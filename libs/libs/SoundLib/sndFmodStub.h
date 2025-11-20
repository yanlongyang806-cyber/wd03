// sndFmodStub.h

#define USE_STUBS 0

// Class member definitions
#ifdef __cplusplus

namespace FMOD
{
#if USE_STUBS
MemoryTracker::MemoryTracker() { }
unsigned int MemoryTracker::getTotal() { return 0;}
void MemoryTracker::clear() {}
#endif //USE_STUBS


#if USE_STUBS
unsigned int MemoryTracker::get(int) { return 0; }

//FMOD_EventSystem_GetParentGroup
FMOD_RESULT Event::getState(unsigned int*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getInfo(int*, char**, FMOD_EVENT_INFO*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getParentGroup(FMOD::EventGroup**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getChannelGroup(FMOD::ChannelGroup**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getPropertyByIndex(int, void*, bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setPropertyByIndex(int, void*, bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getParameter(char const*, FMOD::EventParameter**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getVolume(float*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setVolume(float) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getUserData(void**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setUserData(void*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setProperty(char const*, void*, bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::getProperty(char const*, void*, bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::set3DAttributes(FMOD_VECTOR const*, FMOD_VECTOR const*, FMOD_VECTOR const*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::get3DAttributes(FMOD_VECTOR*, FMOD_VECTOR*, FMOD_VECTOR*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setCallback(FMOD_RESULT (*)(FMOD_EVENT*, FMOD_EVENT_CALLBACKTYPE, void*, void*, void*), void*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::stop(bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::start() { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT Event::setPaused(bool) { return FMOD_ERR_BADCOMMAND; }

FMOD_RESULT EventParameter::setValue(float) { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT Channel::getCurrentSound(Sound **sound)  { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT ChannelGroup::getDSPHead(FMOD::DSP**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::addDSP(FMOD::DSP*, FMOD::DSPConnection**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::getVolume(float*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::overrideVolume(float) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::getName(char*, int) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::release() { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::setUserData(void*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::getNumGroups(int *numgroups) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT ChannelGroup::getChannel(int index, Channel **channel) { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT System::getVersion(unsigned int*)  { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::set3DSettings(float, float, float) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::setAdvancedSettings(FMOD_ADVANCEDSETTINGS*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getDriverInfo(int, char*, int, FMOD_GUID*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getDriverCaps(int, unsigned int*, int*, int*, FMOD_SPEAKERMODE*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::setSpeakerMode(FMOD_SPEAKERMODE) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::setOutput(FMOD_OUTPUTTYPE) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::createDSPByType(FMOD_DSP_TYPE, FMOD::DSP**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::createChannelGroup(char const*, FMOD::ChannelGroup**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getCPUUsage(float*, float*, float*, float*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getDSPHead(FMOD::DSP**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getSoftwareFormat(int*, FMOD_SOUND_FORMAT*, int*, int*, FMOD_DSP_RESAMPLER*, int*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getDSPBufferSize(unsigned int*, int*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::setFileSystem(FMOD_RESULT (*)(char const*, int, unsigned int*, void**, void**), FMOD_RESULT (*)(void*, void*), FMOD_RESULT (*)(void*, void*, unsigned int, unsigned int*, void*), FMOD_RESULT (*)(void*, unsigned int, void*), int) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::getWaveData(float *wavearray, int numvalues, int channeloffset) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT System::createGeometry(int maxpolygons, int maxvertices, Geometry **geometry)  { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT DSP::getNumInputs(int*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::disconnectFrom(FMOD::DSP*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::getInput(int, FMOD::DSP**, FMOD::DSPConnection**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::addInput(DSP *target, DSPConnection **connection) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT DSP::getUserData(void**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::setUserData(void*) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::disconnectAll(bool, bool) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::release() { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::setParameter(int, float) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::getOutput(int index, DSP **output, DSPConnection **outputconnection) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSP::remove() { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT DSPConnection::getInput(FMOD::DSP**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSPConnection::getOutput(FMOD::DSP**) { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT DSPConnection::getMix(float*) { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT Geometry::addPolygon(float, float, bool, int, FMOD_VECTOR const*, int*) { return FMOD_ERR_BADCOMMAND;}

FMOD_RESULT NetEventSystem_Shutdown() { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT NetEventSystem_Update() { return FMOD_ERR_BADCOMMAND;}
FMOD_RESULT NetEventSystem_Init(FMOD::EventSystem*, unsigned short) { return FMOD_ERR_BADCOMMAND;}

//FMOD_RESULT Memory_GetStats  (int *currentalloced, int *maxalloced) { return FMOD_ERR_BADCOMMAND; }
//FMOD_RESULT EventSystem_Create(EventSystem **eventsystem) { return FMOD_ERR_BADCOMMAND; }
//FMOD_RESULT Debug_SetLevel(FMOD_DEBUGLEVEL level)  { return FMOD_ERR_BADCOMMAND; }

#endif // USE_STUBS


}// namespace FMOD

#endif


#ifdef __cplusplus
extern "C" 
{
#endif

#ifdef __cplusplus
} // extern "C"
#endif

#ifndef __cplusplus

#if USE_STUBS

// c function definitions

// EventProject functions
FMOD_RESULT F_API FMOD_EventProject_GetInfo          (FMOD_EVENTPROJECT *eventproject, int *index, char **name) { return FMOD_ERR_BADCOMMAND;}

// EventGroup functions
FMOD_RESULT F_API FMOD_EventGroup_GetParentProject   (FMOD_EVENTGROUP *eventgroup, FMOD_EVENTPROJECT **project) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_EventGroup_GetParentGroup     (FMOD_EVENTGROUP *eventgroup, FMOD_EVENTGROUP **group) { return FMOD_ERR_BADCOMMAND; }

// Event functions
FMOD_RESULT F_API FMOD_Event_GetInfo                 (FMOD_EVENT *event, int *index, char **name, FMOD_EVENT_INFO *info) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_Event_GetParentGroup          (FMOD_EVENT *event, FMOD_EVENTGROUP **group) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_Event_GetProperty             (FMOD_EVENT *event, const char *propertyname, void *value, FMOD_BOOL this_instance) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_Event_SetPropertyByIndex      (FMOD_EVENT *event, int propertyindex, void *value, FMOD_BOOL this_instance) { return FMOD_ERR_BADCOMMAND; }

FMOD_RESULT F_API FMOD_Memory_Initialize           (void *poolmem, int poollen, FMOD_MEMORY_ALLOCCALLBACK useralloc, FMOD_MEMORY_REALLOCCALLBACK userrealloc, FMOD_MEMORY_FREECALLBACK userfree, FMOD_MEMORY_TYPE memtypeflags) { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_Memory_GetStats             (int *currentalloced, int *maxalloced)  { return FMOD_ERR_BADCOMMAND; }

FMOD_RESULT F_API FMOD_EventSystem_Create(FMOD_EVENTSYSTEM **eventsystem)   { return FMOD_ERR_BADCOMMAND; }
FMOD_RESULT F_API FMOD_Debug_SetLevel              (FMOD_DEBUGLEVEL level)   { return FMOD_ERR_BADCOMMAND; }

#endif //USE_STUBS

#endif


