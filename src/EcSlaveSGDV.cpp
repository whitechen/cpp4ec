#include "EcSlaveSGDV.h"
#include "EcUtil.h"

#include <sys/mman.h>
#include "EcSlaveSGDV.h"
#include "EcSlaveFactory.h"

#include <pugixml.hpp>
#include <stdint.h>
#include <stdlib.h> 
#include <unistd.h>

namespace cpp4ec
{

EcSlaveSGDV::EcSlaveSGDV (ec_slavet* mem_loc) : EcSlave (mem_loc),
    useDC (true), SYNC0TIME (1000000), SHIFT (125000),SHIFTMASTER (1000000), PDOerrorsTolerance (9),
    recieveEntry(0),transmitEntry(0), 
    controlWordEntry(0), targetPositionEntry(0), targetVelocityEntry(0), targetTorqueEntry(0),
    statusWordEntry(0), actualPositionEntry(0), actualVelocityEntry(0), actualTorqueEntry(0),
    wControlWordCapable(false), wPositionCapable(false), wVelocityCapable(false),wTorqueCapable(false),
    rStatusWordCapable(false), rPositionCapable(false), rVelocityCapable(false), rTorqueCapable(false),
    pBufferOut(NULL),pBufferIn(NULL),m_mutex(m_slave_nr-1),inputBuf(NULL),outputBuf(NULL)
{
   m_params.resize(0);
   inputObjects.resize(0);
   outputObjects.resize(0);
   bufferList.resize(0);
   m_name = "SGDV_" + to_string(m_datap->configadr & 0x0f,std::dec);  
   
   readXML();  
}

EcSlaveSGDV::~EcSlaveSGDV()
{
    for( int i = 0; i < bufferList.size(); i++)
           delete[] bufferList[i];
    
}
// void EcSlaveSGDV::update()
// {
//     slaveInMutex.lock();
//     memcpy(pBufferOut,outputBuf, outputSize);
//     slaveInMutex.unlock();
// }
    
void EcSlaveSGDV::update()
{
    slaveInMutex.lock();
    memcpy(inputBuf,pBufferIn, inputSize);
    slaveInMutex.unlock();
    int32_t position;
    int32_t velocity;
    int16_t torque;
    readPosition (position);
    readVelocity (velocity);
    readTorque (torque);
    //signal
    slaveValues(position,torque, velocity);

    
}


const std::string& EcSlaveSGDV::getName() const
{
    return m_name;
}

bool EcSlaveSGDV::configure() throw(EcErrorSGDV)
{
    for (unsigned int i = 0; i < m_params.size(); i++)
    {
      ec_SDOwrite(m_slave_nr, m_params[i].index, m_params[i].subindex, FALSE,
		  m_params[i].size,&(m_params[i].param),EC_TIMEOUTRXM);
      if(EcatError)
	throw(EcErrorSGDV(EcErrorSGDV::ECAT_ERROR,m_slave_nr,getName()));

    }
    inputSize  = inputObjects[inputObjects.size()-1].offset + inputObjects[inputObjects.size()-1].byteSize;
    outputSize = outputObjects[outputObjects.size()-1].offset + outputObjects[outputObjects.size()-1].byteSize;
    inputBuf = new char[inputSize];
    outputBuf = new char[outputSize];
    memset(outputBuf,0, outputSize);
    memset(inputBuf,0, inputSize);



    std::cout << getName() << " configured !" <<std::endl;
    
    return true;
}

std::vector<char*> EcSlaveSGDV::start() throw(EcErrorSGDV)
{
  char * temp1 = new char[outputSize];
  char * temp2 = new char[outputSize];
  char * temp3 = new char[outputSize];
  
  writeControlWord(CW_SHUTDOWN);
  slaveOutMutex.lock();
  memcpy(temp1,pBufferOut,outputSize);
  slaveOutMutex.unlock();
  bufferList.push_back(temp1);
  
  writeControlWord(CW_SWITCH_ON);
  slaveOutMutex.lock();
  memcpy(temp2,pBufferOut,outputSize);
  slaveOutMutex.unlock();
  bufferList.push_back(temp2);
  
  // Enable movement
  writeControlWord(CW_ENABLE_OP);
  slaveOutMutex.lock();
  memcpy(temp3,pBufferOut,outputSize);
  slaveOutMutex.unlock();
  bufferList.push_back(temp3);
  
  return bufferList;
}

void EcSlaveSGDV::setPDOBuffer(char * input, char * output)
{
    pBufferIn=input;
    pBufferOut=output;
}

std::vector<char*> EcSlaveSGDV::stop() throw(EcErrorSGDV)
{
  for( int i = 0; i < bufferList.size(); i++)
      delete[] bufferList[i];
  delete[] inputBuf;
  delete[] outputBuf;
      
  bufferList.resize(0);
  char * temp1 = new char[outputSize];
  char * temp2 = new char[outputSize];
      
  writeControlWord(CW_SHUTDOWN);
  memcpy(temp1,pBufferOut,outputSize);
  bufferList.push_back(temp1);
    
  writeControlWord(CW_QUICK_STOP);
  memcpy(temp2,pBufferOut,outputSize);
  bufferList.push_back(temp2);
    
  return bufferList;
}

bool EcSlaveSGDV::writePDO (EcPDOEntry entry, int value)
{
    if(entry < 0 || entry >= outputObjects.size())
    {
        std::cout<<"entry "<<entry<<std::endl;
       	std::cout<<"outputsize "<<outputObjects.size()<<std::endl;
       	throw(EcErrorSGDV(EcErrorSGDV::WRONG_ENTRY_ERROR,m_slave_nr,getName()));
    }
    //write on the desired position of the PDO
    slaveOutMutex.lock();
    memcpy (pBufferOut + outputObjects[entry].offset, &value ,outputObjects[entry].byteSize);
    slaveOutMutex.unlock();
}

bool EcSlaveSGDV::readPDO (EcPDOEntry entry, int& value)
{
    if(entry<0 || entry>=inputObjects.size())
	throw(EcErrorSGDV(EcErrorSGDV::WRONG_ENTRY_ERROR,m_slave_nr,getName()));
    //read the desired position of the PDO
    slaveInMutex.lock();
    memcpy (&value, inputBuf + inputObjects[entry].offset, inputObjects[entry].byteSize);
    slaveInMutex.unlock();
}


bool EcSlaveSGDV::writeControlWord (uint16_t controlWord)
{
    if (!wControlWordCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveOutMutex.lock();
    memcpy (pBufferOut + outputObjects[controlWordEntry].offset, &controlWord ,outputObjects[controlWordEntry].byteSize);
    slaveOutMutex.unlock();
}
bool EcSlaveSGDV::readStatusWord (uint16_t &statusWord)
{
    if (!rStatusWordCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveInMutex.lock();
    memcpy (&statusWord ,inputBuf + inputObjects[statusWordEntry].offset, inputObjects[statusWordEntry].byteSize);
    slaveInMutex.unlock();
    
} 
bool EcSlaveSGDV::writePosition (int32_t position)
{
    if (!wPositionCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveOutMutex.lock();
    memcpy (pBufferOut + outputObjects[targetPositionEntry].offset, &position ,outputObjects[targetPositionEntry].byteSize);
    slaveOutMutex.unlock();
}
bool EcSlaveSGDV::readPosition (int32_t &position)
{
    if (!rPositionCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveInMutex.lock();
    memcpy (&position ,inputBuf + inputObjects[actualPositionEntry].offset, inputObjects[actualPositionEntry].byteSize);
    slaveInMutex.unlock();
}
bool EcSlaveSGDV::writeVelocity (int32_t velocity)
{
    if (!wVelocityCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveOutMutex.lock();
    memcpy (pBufferOut + outputObjects[targetVelocityEntry].offset, &velocity ,outputObjects[targetVelocityEntry].byteSize);
    slaveOutMutex.unlock();
}
bool EcSlaveSGDV::readVelocity (int32_t &velocity)
{
    if (!rVelocityCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveInMutex.lock();
    memcpy (&velocity ,inputBuf + inputObjects[actualVelocityEntry].offset, inputObjects[actualVelocityEntry].byteSize);
    slaveInMutex.unlock();
}
bool EcSlaveSGDV::writeTorque (int16_t torque)
{
    if (!wTorqueCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveOutMutex.lock();
    memcpy (pBufferOut + outputObjects[targetTorqueEntry].offset, &torque ,outputObjects[targetTorqueEntry].byteSize);
    slaveOutMutex.unlock();
}
bool EcSlaveSGDV::readTorque (int16_t &torque)
{
    if (!rTorqueCapable)
	throw(EcErrorSGDV(EcErrorSGDV::FUNCTION_NOT_ALLOWED_ERROR,m_slave_nr,getName()));
    slaveInMutex.lock();
    memcpy (&torque ,inputBuf + inputObjects[actualTorqueEntry].offset, inputObjects[actualTorqueEntry].byteSize);
    slaveInMutex.unlock();
}

void EcSlaveSGDV::readXML() throw(EcErrorSGDV)
{
  parameter temp;  
  std::string xml_name = "configure_SGDV_"+to_string(m_slave_nr,std::dec)+".xml";
  pugi::xml_document doc;
  pugi::xml_parse_result result = doc.load_file(xml_name.c_str());
  if (!result)
    throw(EcErrorSGDV(EcErrorSGDV::XML_NOT_FOUND_ERROR,m_slave_nr,getName()));  
  
  pugi::xml_node parameters = doc.first_child();
  for (pugi::xml_node structure = parameters.first_child(); structure; structure = structure.next_sibling())
  {
    for (pugi::xml_node param = structure.first_child(); param; param = param.next_sibling())
    {
        std::string type = std::string(param.attribute("type").value());
	std::string PDOentry = std::string(param.attribute("PDOentry").value());
	std::string name = std::string(param.attribute("name").value());
	
      	if (name ==  "description")
	{
	  if (type != "string")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));
	  temp.description = param.child_value();
	 
	}
	else if (name ==  "name")
	{
	  if (type != "string")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));
	    temp.name = param.child_value();
	}
	else if (name == "index")
	{
	  if (type != "integer")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));	  
	  temp.index = (int16_t) strtol (param.child_value(),NULL,0);          
	}
	else if (name ==  "subindex")
	{
	  if (type != "integer")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));
	  temp.subindex = (int8_t) strtol (param.child_value(),NULL,0);
	}
	else if (name ==  "size")
	{
	  if (type != "integer")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));	  
	  temp.size = (int8_t) strtol (param.child_value(),NULL,0);
	}
	else if (name ==  "value")
	{
	  if (type != "integer")
	    throw(EcErrorSGDV(EcErrorSGDV::XML_TYPE_ERROR,m_slave_nr,getName()));
	  temp.param = strtol (param.child_value(),NULL,0);
	  addPDOobject(PDOentry,temp.param,temp.subindex);
	}
	
	
	else
	 throw(EcErrorSGDV(EcErrorSGDV::XML_STRUCTURE_ERROR,m_slave_nr,getName()));
    }
    m_params.push_back(temp);
  }
  if(outputObjects.size() <= 0)
      throw(EcErrorSGDV(EcErrorSGDV::OUTPUT_OBJECTS_ERROR,m_slave_nr,getName()));
  if(inputObjects.size() <= 0)
      throw(EcErrorSGDV(EcErrorSGDV::INPUT_OBJECTS_ERROR,m_slave_nr,getName()));

}

bool EcSlaveSGDV::addPDOobject (std::string PDOentry, int value, int subindex)
{
    if ( ((PDOentry != "transmit") && (PDOentry != "recieve")) || subindex == 0 )
    {
	return false;
    }
    int mask1 = 0xFFFF0000;
    int mask2 = 0x0000FFFF;
    int objectNumber = (value & mask1) >> 16;
    int objectSize = (value & mask2)/8;
    if (PDOentry == "transmit")
    {
	PDOobject temp;
	if(transmitEntry>0)
	{
	  temp.offset = inputObjects[transmitEntry-1].offset + inputObjects[transmitEntry-1].byteSize;
 	}else{
          temp.offset = 0;
	}
	temp.byteSize = objectSize;
	
	switch (objectNumber)
	{
	    case STATUS_WORD:
		statusWordEntry = transmitEntry;
		rStatusWordCapable = true;  
		break;
		
	    case ACTUAL_POSITION:
		actualPositionEntry=transmitEntry;
		rPositionCapable = true; 
		break;
		
	    case ACTUAL_VELOCITY:
		actualVelocityEntry = transmitEntry;
		rVelocityCapable = true;
		break;
		
	    case ACTUAL_TORQUE:
		actualTorqueEntry=transmitEntry;
		rTorqueCapable = true;
		break;
		
	    default:
		break;
		
	}
	inputObjects.push_back(temp);	
	transmitEntry += 1;
    }
    
    if (PDOentry == "recieve")
    {
	PDOobject temp;
	if(recieveEntry>0)
	{
	  temp.offset = outputObjects[recieveEntry-1].offset + outputObjects[recieveEntry-1].byteSize;
        }else{ 	
          temp.offset = 0;
	}
	temp.byteSize = objectSize;
	
	switch (objectNumber)
	{
	    case CONTROL_WORD:
		controlWordEntry = recieveEntry;
		wControlWordCapable = true;  
		break;
		
	    case TARGET_POSITION:
		targetPositionEntry = recieveEntry;
		wPositionCapable = true;
		break;
		
	    case TARGET_VELOCITY:
		targetVelocityEntry = recieveEntry;
		wVelocityCapable = true;
		break;
		
	    case TARGET_TORQUE:
		targetTorqueEntry = recieveEntry;
		wTorqueCapable = true;
		break;
		
	    default:
		break;
		
	}
	outputObjects.push_back(temp);
	recieveEntry += 1;
    }
	
}
    
    
    
void EcSlaveSGDV::setSGDVOject(uint16_t index, uint8_t subindex, int psize, void * param)
{
  ec_SDOwrite(m_slave_nr, index, subindex, FALSE,psize,param,EC_TIMEOUTRXM);
}

void EcSlaveSGDV::getSGDVObject(uint16_t index, uint8_t subindex, int *psize, void *param)
{
  ec_SDOread(m_slave_nr, index, subindex, FALSE,psize,param,EC_TIMEOUTRXM);
}

namespace {
cpp4ec::EcSlave* createEcSlaveSGDV(ec_slavet* mem_loc)
{
	return new EcSlaveSGDV(mem_loc);
}

const bool registered0 = cpp4ec::EcSlaveFactory::Instance().registerDriver("? M:00000539 I:02200001", createEcSlaveSGDV);

}
}

