/*
 * =====================================================================================
 *
 *       Filename:  Phrase-Sequencer.c
 *
 *    Description:  Main file of the MIDI sequencer plugin, I have used some parts of 
 *                  the eg-metro lv2 plugin and the mod-midi-switchbox plugin. 
 *
 *        Version:  1.0
 *        Created:  12/24/2018 03:29:18 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Bram Giesen (), bram@moddevices.com
 *   Organization:  MOD Devices
 *
 * =====================================================================================
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sequencer_utils.h"
#include "oscillators.h"

// Struct for a 3 byte MIDI event
typedef struct {
  LV2_Atom_Event event;
  uint8_t        msg[3];
} LV2_Atom_MIDI;


int file_exist (char *filename)
{
  struct stat   buffer;
  return (stat (filename, &buffer) == 0);
}



static LV2_Handle instantiate(const LV2_Descriptor*     descriptor,
    double                    rate,
    const char*               path,
    const LV2_Feature* const* features)
{
  Data* self = (Data*)calloc(1, sizeof(Data));

  // Get host features
  const LV2_URID_Map* map = NULL;

  for (int i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      map = (const LV2_URID_Map*)features[i]->data;
      break;
    }
  }
  if (!map) {
    free(self);
    return NULL;
  }

  // Map URIs
  self->urid_midiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);

  MetroURIs* const    uris  = &self->uris;
  uris->atom_Blank          = map->map(map->handle, LV2_ATOM__Blank);
  uris->atom_Float          = map->map(map->handle, LV2_ATOM__Float);
  uris->atom_Object         = map->map(map->handle, LV2_ATOM__Object);
  uris->atom_Path           = map->map(map->handle, LV2_ATOM__Path);
  uris->atom_Resource       = map->map(map->handle, LV2_ATOM__Resource);
  uris->atom_Sequence       = map->map(map->handle, LV2_ATOM__Sequence);
  uris->time_Position       = map->map(map->handle, LV2_TIME__Position);
  uris->time_barBeat        = map->map(map->handle, LV2_TIME__barBeat);
  uris->time_beatsPerMinute = map->map(map->handle, LV2_TIME__beatsPerMinute);
  uris->time_beatsPerBar    = map->map(map->handle, LV2_TIME__beatsPerBar);
  uris->time_speed          = map->map(map->handle, LV2_TIME__speed);

  self->rate             = rate;
  self->nyquist          = rate / 2; 
  self->bpm              = 120.0f;
  self->beat             = 0;
  self->barCount         = 0;
  self->recordingStatus  = 0;
  self->divisionRate     = 4;
	self->phase            = 0;
  self->sinePhase        = 0;
  self->amplitude        = 0;
  self->phaseRecord      = 0;
  self->velPhase         = 0.000000009;
  self->x1               = 0.00000001; 
  self->velocityLFO      = 0;
  self->velocity         = 0;
  self->octaveIndex      = 0;
  self->noteOffIndex     = 0; 
  self->noteOffSendIndex = 0; 
  self->countTicks       = 0;
  self->patternIndex     = 0;
  self->modeHandle       = 0;
  self->prevMod          = 100;
  self->prevLatch        = 100;
  self->count            = 0;
  self->activeNoteIndex  = 0;
  self->inputIndex       = 0;
  self->notesPressed     = 0;
  //check this value
  self->prevThrough      = 0;

  self->placementIndex   = 0;
  self->notePlacement[0] = 0;
  self->notePlacement[1] = 0.5;
  self->previousSpeed    = 0;

  //resetPhase vars:
  self->previousDevision = 12;
  self->previousPlaying = false;
  self->resetPhase      = true;

  for (size_t i = 0; i < 16; i++) {
    self->midiThroughInput[i] = 0;
  }
    
	debug_print("test debug\n");
  //init objects
  self->writeEvents  = (Array* )malloc(sizeof(Array));
  self->playEvents   = (Array* )malloc(sizeof(Array));
  self->ARStatus     = IDLE;

  for (size_t i = 0; i < 4; i++) {
    self->noteOffArr[i] = 0;
  }

  for (size_t row = 0; row < 4; row++) {
    for (size_t index = 0; index < 2; index++) {
      self->noteOffTimer[row][index] = 0;
    }
  }

  for (size_t voice = 0; voice < 4; voice++) {
    for (uint8_t note = 0; note < 248; note++) {
      for (size_t noteProp = 0; noteProp < 2; noteProp++) {
        self->writeEvents->eventList[voice][note][noteProp] = 0;
        self->playEvents->eventList[voice][note][noteProp] = 0;
      }
    }
  }
  //init vars
  self->writeEvents->used  = 0;
  self->playEvents->used   = 0;

  self->notePlayed  = 0;
  self->transpose   = 0;
  self->noteFound   = 0;


  self->firstRecordedNote  = false; 
  self->barCounted         = false;
  self->startPreCount      = false;
  self->recording          = false; 
  self->trigger            = false;
  self->triggerSet         = false;
  self->preCountTrigger    = false;
  self->cleared            = true; 
  self->through            = true;
  self->firstBar           = false;
  self->playing            = false;
  self->clip               = false;
  self->alreadyPlaying     = false;
  self->recordingTriggered = false;

	//init pointer for velocity pattern
	self->pattern[0] = &self->patternVel1;
	self->pattern[1] = &self->patternVel2;
	self->pattern[2] = &self->patternVel3;
	self->pattern[3] = &self->patternVel4;
	self->pattern[4] = &self->patternVel5;
	self->pattern[5] = &self->patternVel6;
	self->pattern[6] = &self->patternVel7;
	self->pattern[7] = &self->patternVel8;


  return self;
}



static void 
connect_port(LV2_Handle instance, uint32_t port, void* data)
{
  Data* self = (Data*)instance;

  switch (port)
  {
		case PORT_ATOM_IN:
			self->port_events_in = (const LV2_Atom_Sequence*)data;
			break;
		case PORT_ATOM_OUT1:
			self->port_events_out1 = (LV2_Atom_Sequence*)data;
			break;
    case PORT_METROME_OUT:
      self->metroOut = (float*)data; 
      break;
		case METRO_CONTROL:
			self->control = (LV2_Atom_Sequence*)data;
			break;
		case MODE:
			self->mode = (const float*)data;
			break;
		case ACTIVATERECORDING:
			self->recordTrigger = (float*)data;
			break;
		case DIVISION:
			self->division = (const float*)data;
			break;
		case NOTELENGTH:
			self->noteLengthParam = (const float*)data;
			break;
		case OCTAVESPREAD:
			self->octaveSpread = (const float*)data;
			break;
		case TRANSPOSE:
			self->latchTranspose = (const float*)data;
			break;
		case SWING:
			self->swing = (const float*)data;
			break;
		case RANDOMIZETIMMING:
			self->randomizeTimming = (const float*)data;
			break;
		case VELOCITYMODE:
			self->velocityMode = (const float*)data;
			break;
		case VELOCITYCURVE:
			self->velocityCurve = (const float*)data;
			break;
		case CURVEDEPTH:
			self->curveDepth = (const float*)data;
			break;
		case CURVECLIP:
			self->curveClip = (const float*)data;
			break;
		case CURVELENGTH:
			self->curveLength = (const float*)data;
			break;
		case VELOCITYPATTERNLENGTH:
			self->velocityPatternLength = (const float*)data;
			break;
		case PATTERNVEL1:
			self->patternVel1 = (const float*)data;
			break;
		case PATTERNVEL2:
			self->patternVel2 = (const float*)data;
			break;
		case PATTERNVEL3:
			self->patternVel3 = (const float*)data;
			break;
		case PATTERNVEL4:
			self->patternVel4 = (const float*)data;
			break;
		case PATTERNVEL5:
			self->patternVel5 = (const float*)data;
			break;
		case PATTERNVEL6:
			self->patternVel6 = (const float*)data;
			break;
		case PATTERNVEL7:
			self->patternVel7 = (const float*)data;
			break;
		case PATTERNVEL8:
			self->patternVel8 = (const float*)data;
			break;
  }
}



static void 
activate(LV2_Handle instance)
{

}


//create a midi message 
static LV2_Atom_MIDI 
createMidiEvent(Data* self, uint8_t status, uint8_t note, uint8_t velocity)
{ 
  LV2_Atom_MIDI msg;
  memset(&msg, 0, sizeof(LV2_Atom_MIDI));

  msg.event.body.size = 3;
  msg.event.body.type = self->urid_midiEvent;

  msg.msg[0] = status;
  msg.msg[1] = note;
  msg.msg[2] = velocity;

  return msg;
}



static void
update_position(Data* self, const LV2_Atom_Object* obj)
{
  const MetroURIs* uris = &self->uris;

  // Received new transport position/speed
  LV2_Atom *beat = NULL, *bpm = NULL, *speed = NULL, *barsize = NULL;
  lv2_atom_object_get(obj,
      uris->time_barBeat, &beat,
      uris->time_beatsPerMinute, &bpm,
      uris->time_speed, &speed, uris->time_beatsPerBar, &barsize, 
      NULL);

  self->previousSpeed = 0; 

  if (bpm && bpm->type == uris->atom_Float) {
    // Tempo changed, update BPM
    self->bpm = ((LV2_Atom_Float*)bpm)->body;
  }
  if (speed && speed->type == uris->atom_Float) {
    // Speed changed, e.g. 0 (stop) to 1 (play)
    self->speed = ((LV2_Atom_Float*)speed)->body;
  }
  if (beat && beat->type == uris->atom_Float) {
    const float bar_beats       = ((LV2_Atom_Float*)beat)->body;
    const float beat_beats      = bar_beats - floorf(bar_beats);
    const float beat_barsize    = ((LV2_Atom_Float*)barsize)->body;  
    self->beat                  = bar_beats - floorf(bar_beats);
    self->beatInMeasure = ((LV2_Atom_Float*)beat)->body; 
    self->barsize = beat_barsize; 
    
    if (self->speed != self->previousSpeed) {
      self->phase = beat_beats;
      self->previousSpeed = self->speed;
    }
  }
}


//clear all arrays and set values back to initial state TODO check adding trigger?
static void
clearNotes(Data *self, const uint32_t outCapacity)
{
  //TODO only note offs for notes that are currently being played
  for (size_t mNotes = 0; mNotes < 127; mNotes++) {
    LV2_Atom_MIDI msg = createMidiEvent(self, 128, mNotes, 0);
    lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&msg);
  }
}


static void
stopSequence(Data* self)
{
  clearSequence(self->writeEvents);
  clearSequence(self->playEvents);

  self->writeEvents->used  = 0;
  self->playEvents->used   = 0;
  self->activeNotes        = 0;
  self->transpose          = 0;
  self->firstBar           = false;
  self->trigger            = false;
  self->octaveIndex        = 0;
  self->notePlayed         = 0;
  self->octaveIndex        = 0;
  self->noteTie            = 0;
  
  for (int y = 0; y < 2; y++) {
    self->noteStarted[y] = 0;
  }

  self->noteOffIndex     = 0;
  self->noteOffSendIndex = 0; 
  self->activeNotes      = 0;
  self->cleared          = true;
}



static uint32_t 
handlePorts(Data* self)
{
  const uint32_t outCapacity = self->port_events_out1->atom.size;
  // Write an empty Sequence header to the outputs
  lv2_atom_sequence_clear(self->port_events_out1);
  self->port_events_out1->atom.type = self->port_events_in->atom.type;
 
  return outCapacity; 
}

static float
applyRandomTiming(Data* self)
{
  return *self->randomizeTimming * ((rand() % 100) * 0.003);
}


static uint8_t 
octaveHandler(Data* self)
{
  uint8_t octave = 12 * self->octaveIndex; 
  self->octaveIndex = (self->octaveIndex + 1) % (int)*self->octaveSpread;
  
  return octave;
}


static uint8_t 
velocityHandler(Data* self)
{

  //TODO create function for velocity handling
  if ((int)*self->velocityMode == 0) {
    self->velocity = 80;
  } else if ((int)*self->velocityMode == 1) { 
    self->velocity = 127 + (int)floor(((self->velocityLFO) - 127) * *self->curveDepth);
  } else if ((int)*self->velocityMode == 2) {
    self->velocity = (uint8_t)floor(**self->pattern[self->patternIndex]);
  }
  
  self->patternIndex = (self->patternIndex + 1) % (int)*self->velocityPatternLength; 

  if (self->clip)
    self->clip = false;

  return self->velocity;
}



static void 
handleNoteOn(Data* self, const uint32_t outCapacity)
{
  //get octave and velocity
  for (size_t voices = 0; voices < 4; voices++) { 
    debug_print("voices in play = %li\n", voices);
    if ( self->playEvents->eventList[voices][self->notePlayed][0] > 0 && self->playEvents->eventList[voices][self->notePlayed][0] < 128)
    {
      debug_print("play a note");
      uint8_t octave = octaveHandler(self);
      uint8_t velocity = velocityHandler(self);

      //create MIDI note on message
      uint8_t midiNote = self->playEvents->eventList[voices][self->notePlayed][0] + self->transpose + octave;

      //check if note is already playing
      for (size_t i = 0; i < 4; i++) {
        if ((uint8_t)self->noteOffTimer[i][0] == midiNote) { 
          self->noteOffTimer[i][1] = 0;
          self->alreadyPlaying = true;
          self->noteFound = i; 
        } else {
          self->alreadyPlaying = false;
        }
      }

      if (!self->alreadyPlaying) {
        //send MIDI note on message
        LV2_Atom_MIDI onMsg = createMidiEvent(self, 144, midiNote, velocity);
        lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&onMsg);

        if (self->noteTie > 0) {
          //sendNoteOff
          LV2_Atom_MIDI noteTieMsg = createMidiEvent(self, 128, self->noteTie, 0);
          lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&noteTieMsg);
          self->noteTie = 0;
        }

        if (self->playEvents->eventList[voices][self->notePlayed][1] != 2) { 
          self->noteOffTimer[self->activeNoteIndex][0] = (float)midiNote;
          self->activeNoteIndex = (self->activeNoteIndex + 1) % 4; 
        } else {
          self->noteTie = midiNote;
        }
      } else {
        self->activeNoteIndex = (self->noteFound + 1) % 4; 
      }

      //check for note tie else add to noteOffTimer
    } else if (self->noteTie > 0) {
      //TODO check for better structure, this code is included twice because it wasn't working when playing a rest
      LV2_Atom_MIDI noteTieMsg = createMidiEvent(self, 128, self->noteTie, 0);
      lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&noteTieMsg);
      self->noteTie = 0;
    }

    //set boolean for active notes send and set boolean for trigger to prevent multiple triggers
    self->cleared = false;
    self->trigger = true;

  }
  //increment sequence index 
  self->notePlayed++;
  self->notePlayed = (self->notePlayed > (self->playEvents->used - 1)) ? 0 : self->notePlayed;
}



static void
handleNoteOff(Data* self, const uint32_t outCapacity)
{
  for (int i = 0; i < 4; i++) {
    if (self->noteOffTimer[i][0] > 0) {
      self->noteOffTimer[i][1] += self->frequency / self->rate;
      if (self->noteOffTimer[i][1] > *self->noteLengthParam) {
        LV2_Atom_MIDI offMsg = createMidiEvent(self, 128, (uint8_t)self->noteOffTimer[i][0], 0);
        lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&offMsg);
        self->noteOffTimer[i][0] = 0;
        self->noteOffTimer[i][1] = 0;
      }
    }  
  }
}



static int
switchMode(Data* self, const uint32_t outCapacity)
{
  ModeEnum modeStatus    = (int)*self->mode;
  //TODO set normal value 
  if ((int)*self->mode != self->prevMod || (int)*self->latchTranspose != self->prevLatch) 
  {
    switch (modeStatus)
    {
      case CLEAR_ALL:
        clearNotes(self, outCapacity);
        stopSequence(self);
        self->playing    = false;
        self->through    = true; 
        break;
      case STOP:
        self->recordingStatus = 0;
        self->notePlayed      = 0;
        self->through         = true; 
        self->playing         = false;
        break;
      case PLAY:
        if (self->writeEvents->used > 0 && !self->recording)
          self->playing = true;
        break;
      case RECORD_OVERWRITE:
        break;
      case RECORD_APPEND: 
        break;
      case UNDO_LAST:
        //TODO works but it should be aware of sequence
        self->writeEvents->used--;
        break;
    }
    self->prevMod = (int)*self->mode;
    self->prevLatch = (int)*self->latchTranspose;
  }
  if (*self->recordTrigger == 1 && !self->recordingTriggered) {
    debug_print("self->recordTrigger = %f\n", *self->recordTrigger);
    *self->recordTrigger = 0;
    self->recordingTriggered = true;
    self->startPreCount = true;
  }

  return self->modeHandle;
}


/*function that handles the process of starting the pre-count at the beginning of next bar,
pre-count length and recording length.*/
static void 
handleBarSyncRecording(Data *self)
{
  switch(self->recordingStatus)
  {
    case 0: //start pre-counting at next bar
      if (self->beatInMeasure < 0.01 && self->startPreCount) {
        debug_print("self->beatInMeasure while waiting = %f\n", self->beatInMeasure);
        self->startPreCount = false;
        self->recordingStatus = 1;
      }
//      debug_print("WAITING FOR FIRST BAR\n"); 
      break;
    case 1: //count bars while pre-counting
      precount(self);
      self->recordingStatus = barCounter(self, 0);
//      debug_print("PRE-COUNTING\n"); 
      break;
    case 2: //record
      self->recording = true;
      self->phaseRecord = *phaseRecord(self->frequency, &self->phaseRecord, self->rate);
      self->recordingStatus = (barCounter(self, 3)) + 1;
//      debug_print("RECORDING\n"); 
      break;
    case 3: //stop recording 
//      debug_print("STOP RECORDING\n"); 
      self->recording   = false;
      self->recordingTriggered = false;
      self->startPreCount = false;
      self->phaseRecord = 0;
      copyEvents(self->writeEvents, self->playEvents);
      self->recordingStatus = 0;
      self->playing = true;
      break;
  }
}

static void 
handleNotes(Data* self, const uint8_t* const msg, uint8_t status, int modeHandle, uint32_t outCapacity, void* ev)
{
  
  switch (status)
  {

    case LV2_MIDI_MSG_NOTE_ON:
      self->notesPressed++;  
      uint8_t midiNote;
      midiNote = msg[1];
      debug_print("message = %i\n", msg[1]);

      if (self->recording) {
        recordNotes(self, midiNote);
        debug_print("midiNote in RECORD = %i\n", midiNote);
      }
      break;
    case LV2_MIDI_MSG_NOTE_OFF:
      self->notesPressed--;
      break;
  }
   
	//MIDI through   
	if (self->through) {
    self->midiThroughInput[self->inputIndex++ % 16] = msg[1]; 
    lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, ev);
    self->prevThrough = 1;
  } 
  else if (self->prevThrough == 1) {
    for (size_t i = 0; i < self->inputIndex + 1; i++) {
      //send note off
      //TODO does this need an init value?
      LV2_Atom_MIDI offMsg = createMidiEvent(self, 128, self->midiThroughInput[i], 0);
      lv2_atom_sequence_append_event(self->port_events_out1, outCapacity, (LV2_Atom_Event*)&offMsg);
    } 
    self->inputIndex  = 0;
    self->prevThrough = 0;
  }
}



static void
sequenceProcess(Data* self, const uint32_t outCapacity)
{
  //placement is used to control the amount of swing, the place of the of [0] is static the placement of note [1] can -
  //be moved  
  self->notePlacement[1] = *self->swing * 0.01;

  if (self->playing) 
  {
    debug_print("self->playEvents->used = %li\n", self->playEvents->used);
    float offset = applyRandomTiming(self); 
    if (self->phase >= self->notePlacement[self->placementIndex] && self->phase < (self->notePlacement[self->placementIndex] + 0.2) 
        && !self->trigger && self->playEvents->used > 0) 
    { 
      debug_print("handleNoteOn is triggered");
      handleNoteOn(self, outCapacity); 
      self->triggerSet = false;
    } else 
    { //if this is false: (self->phase < 0.2 && !trigger && self->writeEvents->used > 0)
      if (self->phase > self->notePlacement[self->placementIndex] + 0.2 && !self->triggerSet) 
      {
        self->placementIndex ^= 1;
        self->trigger = false;
        //TODO does this trigger has to be reset as well?
        self->triggerSet = true;    
      }
    }
  }  
  handleNoteOff(self, outCapacity); 
}


//sequence the MIDI notes that are written into an array
static void 
handleEvents(Data* self, const uint32_t outCapacity)
{
  int modeHandle = switchMode(self, outCapacity);
  
  // Read incoming events
  LV2_ATOM_SEQUENCE_FOREACH(self->port_events_in, ev)
  {
    if (ev->body.type == self->urid_midiEvent)
    {
      const uint8_t* const msg = (const uint8_t*)(ev + 1);
      const uint8_t status = msg[0] & 0xF0;
      handleNotes(self, msg, status, modeHandle, outCapacity, ev);
    }
  }
}



static void 
run(LV2_Handle instance, uint32_t n_samples)
{
  Data* self = (Data*)instance;

	self->port_events_out1->atom.type = self->port_events_in->atom.type;

  const MetroURIs* uris = &self->uris;
  
  // Work forwards in time frame by frame, handling events as we go
  const LV2_Atom_Sequence* in     = self->control;

  for (const LV2_Atom_Event* ev = lv2_atom_sequence_begin(&in->body);
      !lv2_atom_sequence_is_end(&in->body, in->atom.size, ev);
      ev = lv2_atom_sequence_next(ev)) {

    if (ev->body.type == uris->atom_Object ||
        ev->body.type == uris->atom_Blank) {
      const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
      if (obj->body.otype == uris->time_Position) {
        // Received position information, update
        update_position(self, obj);
      }
    }
  }
  self->frequency = calculateFrequency(self->bpm, self->divisionRate);
  //halftime speed when frequency goes out of range
  if (self->frequency > self->nyquist)
    self->frequency = self->frequency / 2;
  
  
  //get the capacity
  const uint32_t outCapacity = handlePorts(self); 
  
  //a phase Oscillator that we use for the tempo of the midi-sequencer
  for (uint32_t pos = 0; pos < n_samples; pos++) {
    self->phase = *phaseOsc(self->frequency, &self->phase, self->rate, *self->swing);
    self->velocityLFO = *velOsc(self->frequency, &self->velocityLFO, self->rate, self->velocityCurve, self->curveDepth,
        self->curveLength, self->curveClip, self);
    handleBarSyncRecording(self);
    attackRelease(self);
    self->metroOut[pos] = 0.1 * self->amplitude * (float)sinOsc(440, &self->sinePhase, self->rate);
    sequenceProcess(self, outCapacity);
  }
  handleEvents(self, outCapacity);
}



static void cleanup(LV2_Handle instance)
{
  free(instance);
}



static const LV2_Descriptor descriptor = {
  .URI = "http://moddevices.com/plugins/mod-devel/Phrase-Sequencer",
  .instantiate = instantiate,
  .connect_port = connect_port,
  .activate = activate,
  .run = run,
  .deactivate = NULL,
  .cleanup = cleanup,
  .extension_data = NULL
};




  LV2_SYMBOL_EXPORT
const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
  return (index == 0) ? &descriptor : NULL;
}


