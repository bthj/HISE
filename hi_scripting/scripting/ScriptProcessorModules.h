/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#ifndef SCRIPTPROCESSORMODULES_H_INCLUDED
#define SCRIPTPROCESSORMODULES_H_INCLUDED

namespace hise { using namespace juce;


/** This scripting processor uses the JavaScript Engine to execute small scripts that can change the midi message.
*	@ingroup midiTypes
*
*	A script should have this function:
*
*		function onNoteOn()
*		{
*			// do your stuff here
*		}
*
*	You can use the methods from ScriptingApi to change the midi message.
*
*/
class JavascriptMidiProcessor : public ScriptBaseMidiProcessor,
								public JavascriptProcessor,
								public Timer
{
public:

	SET_PROCESSOR_NAME("ScriptProcessor", "Script Processor", "MIDI Processor that allows scripting.")

	enum SnippetsOpen
	{
		onNoteOnOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		onNoteOffOpen,
		onControllerOpen,
		onTimerOpen,
		onControlOpen,
		externalPopupShown,
		numScriptEditorStates
	};

	JavascriptMidiProcessor(MainController *mc, const String &id);
	~JavascriptMidiProcessor();;

	Path getSpecialSymbol() const override;

	void suspendStateChanged(bool shouldBeSuspended) override
	{
		ScriptBaseMidiProcessor::suspendStateChanged(shouldBeSuspended);

		deferredExecutioner.suspend(shouldBeSuspended);
	}

	ValueTree exportAsValueTree() const override;;
	void restoreFromValueTree(const ValueTree &v) override;
	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return numCallbacks; }
	void registerApiClasses() override;
	

	void addToFront(bool addToFront_) noexcept;;
	bool isFront() const { return front; };

	StringArray getImageFileNames() const;

	/** This defers the callbacks to the message thread.
	*
	*	It stops all timers and clears any message queues.
	*/
	void deferCallbacks(bool addToFront_);
	bool isDeferred() const { return deferred; };

	void timerCallback() override
	{
		jassert(isDeferred());
		runTimerCallback();
	}

	void processHiseEvent(HiseEvent &m) override;

	static JavascriptMidiProcessor* getFirstInterfaceScriptProcessor(MainController* mc)
	{
		Processor::Iterator<JavascriptMidiProcessor> iter(mc->getMainSynthChain());

		while (auto jsp = iter.getNextProcessor())
		{
			if (jsp->isFront())
			{
				return jsp;
			}
		}

		return nullptr;
	}

	StringArray& getListOfModuleIds() {
		return storedModuleIds;
	}

	ScriptingApi::Server::WeakPtr getServerObject() { return serverObject; }

private:

	struct DeferredExecutioner : public LockfreeAsyncUpdater
	{
		DeferredExecutioner(JavascriptMidiProcessor* jp) :
			parent(*jp),
			pendingEvents(512)
		{};

		void addPendingEvent(const HiseEvent& e)
		{
			pendingEvents.push(e);
			triggerAsyncUpdate();
		}

	private:

		void handleAsyncUpdate() override
		{
			jassert(parent.isDeferred());
			
			HiseEvent m;

			while (pendingEvents.pop(m))
			{
				if (m.isIgnored() || m.isArtificial())
					continue;

				auto f = [m](JavascriptProcessor* p)
				{
					auto jmp = dynamic_cast<JavascriptMidiProcessor*>(p);

					HiseEvent copy(m);

					ScopedValueSetter<HiseEvent*> svs(jmp->currentEvent, &copy);
					jmp->currentMidiMessage->setHiseEvent(m);
					jmp->runScriptCallbacks();

					return jmp->lastResult;
				};

				parent.getMainController()->getJavascriptThreadPool().addJob(JavascriptThreadPool::Task::HiPriorityCallbackExecution, &parent, f);
			}
		}

		LockfreeQueue<HiseEvent> pendingEvents;
		JavascriptMidiProcessor& parent;
	};

	DeferredExecutioner deferredExecutioner;


	void runTimerCallback(int offsetInBuffer = -1);
	void runScriptCallbacks();

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> onNoteOnCallback;
	ScopedPointer<SnippetDocument> onNoteOffCallback;
	ScopedPointer<SnippetDocument> onControllerCallback;
	ScopedPointer<SnippetDocument> onControlCallback;
	ScopedPointer<SnippetDocument> onTimerCallback;

	ReadWriteLock defferedMessageLock;

	MidiBuffer deferredMidiMessages;
	MidiBuffer copyBuffer;

	HiseEventBuffer deferredEvents;
	HiseEventBuffer copyEventBuffer;

	ReferenceCountedObjectPtr<ScriptingApi::Message> currentMidiMessage;
	ReferenceCountedObjectPtr<ScriptingApi::Engine> engineObject;

	ScriptingApi::Server::WeakPtr serverObject;

	ScriptingApi::Sampler *samplerObject;
	ScriptingApi::Synth *synthObject;

	bool front, deferred, deferredUpdatePending;

	StringArray storedModuleIds;
};

class JavascriptVoiceStartModulator : public JavascriptProcessor,
									  public ProcessorWithScriptingContent,
									  public VoiceStartModulator
{
public:

	enum Callback
	{
		onInit = 0,
		onVoiceStart,
		onVoiceStop,
		onController,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		onVoiceStartOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		onVoiceStopOpen,
		onControllerOpen,
		onControlOpen,
		externalPopupShown,
		numScriptEditorStates
	};

	SET_PROCESSOR_NAME("ScriptVoiceStartModulator", "Script Voice Start Modulator", "Creates a scriptable modulation value at the start of the voice.")


	JavascriptVoiceStartModulator(MainController *mc, const String &id, int voiceAmount, Modulation::Mode m);;
	~JavascriptVoiceStartModulator();

	Path getSpecialSymbol() const override;

	float getAttribute(int index) const override { return getControlValue(index); }
	void setInternalAttribute(int index, float newValue) override { setControlValue(index, newValue); }

	ValueTree exportAsValueTree() const override { ValueTree v = VoiceStartModulator::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override { VoiceStartModulator::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }
	
	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	float calculateVoiceStartValue(const HiseEvent &/*m*/) override { return 0.0f; };
	virtual void handleHiseEvent(const HiseEvent &m) override;

	/** When the startNote function is called, a previously calculated value (by the handleMidiMessage function) is stored using the supplied voice index. */
	virtual float startVoice(int voiceIndex) override;;

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		return getContentParameterIdentifier(parameterIndex);
	}

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return numCallbacks; }
	void registerApiClasses() override;
	
	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

private:

	ReferenceCountedObjectPtr<ScriptingApi::Message> currentMidiMessage;
	ReferenceCountedObjectPtr<ScriptingApi::Engine> engineObject;

	ScriptingApi::Sampler *samplerObject;
	ScriptingApi::Synth *synthObject;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> onVoiceStartCallback;
	ScopedPointer<SnippetDocument> onVoiceStopCallback;
	ScopedPointer<SnippetDocument> onControllerCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JavascriptVoiceStartModulator)
};


class JavascriptTimeVariantModulator : public JavascriptProcessor,
									   public ProcessorWithScriptingContent,
									   public TimeVariantModulator
{
public:

	SET_PROCESSOR_NAME("ScriptTimeVariantModulator", "Script Time Variant Modulator", "Creates a scriptable monophonic modulation signal.")

	enum Callback
	{
		onInit = 0,
		prepare,
		processBlock,
		onNoteOn,
		onNoteOff,
		onController,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		prepareToPlayOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		processBlockOpen,
		onNoteOnOpen,
		onNoteOffOpen,
		onControllerOpen,
		onControlOpen,
		externalPopupShown,
		numScriptEditorStates
	};

	JavascriptTimeVariantModulator(MainController *mc, const String &id, Modulation::Mode m);
	~JavascriptTimeVariantModulator();

	Path getSpecialSymbol() const override;

	float getAttribute(int index) const override
	{
		if (auto n = getActiveNetwork())
			return n->networkParameterHandler.getParameter(index);
		else
			return contentParameterHandler.getParameter(index);
	}

	void setInternalAttribute(int index, float newValue) override
	{
		if (auto n = getActiveNetwork())
			n->networkParameterHandler.setParameter(index, newValue);
		else
			contentParameterHandler.setParameter(index, newValue);
	}

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		if (auto n = getActiveNetwork())
			return n->networkParameterHandler.getParameterId(parameterIndex);
		else
			return contentParameterHandler.getParameterId(parameterIndex);
	}
	ValueTree exportAsValueTree() const override { ValueTree v = TimeVariantModulator::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override { TimeVariantModulator::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }

	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	void handleHiseEvent(const HiseEvent &m) override;
	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void calculateBlock(int startSample, int numSamples) override;;

	Processor *getChildProcessor(int /*processorIndex*/) override final { return nullptr; };
	const Processor *getChildProcessor(int /*processorIndex*/) const override final { return nullptr; };
	int getNumChildProcessors() const override final { return 0; };

	

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return Callback::numCallbacks; }
	void registerApiClasses() override;
	
	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

	void postCompileCallback() override;

private:

	ReferenceCountedObjectPtr<ScriptingApi::Message> currentMidiMessage;
	ReferenceCountedObjectPtr<ScriptingApi::Engine> engineObject;
	ScriptingApi::Synth *synthObject;

	VariantBuffer::Ptr buffer;
	var bufferVar;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> prepareToPlayCallback;
	ScopedPointer<SnippetDocument> processBlockCallback;
	ScopedPointer<SnippetDocument> onNoteOnCallback;
	ScopedPointer<SnippetDocument> onNoteOffCallback;
	ScopedPointer<SnippetDocument> onControllerCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JavascriptTimeVariantModulator)
};


class JavascriptEnvelopeModulator : public JavascriptProcessor,
								    public ProcessorWithScriptingContent,
									public EnvelopeModulator
{
public:

	SET_PROCESSOR_NAME("ScriptEnvelopeModulator", "Script Envelope Modulator", "Creates a scriptable polyphonic modulation signal.")

	enum Callback
	{
		onInit = 0,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		prepareToPlayOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		onControlOpen,
		externalPopupShown,
		numScriptEditorStates
	};

	JavascriptEnvelopeModulator(MainController *mc, const String &id, int numVoices, Modulation::Mode m);
	~JavascriptEnvelopeModulator();

	Path getSpecialSymbol() const override;

	bool isPolyphonic() const override { return true; }
	
	ValueTree exportAsValueTree() const override { ValueTree v = EnvelopeModulator::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override { EnvelopeModulator::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }

	float getAttribute(int index) const override
	{
		if (auto n = getActiveNetwork())
			return n->networkParameterHandler.getParameter(index);
		else
			return contentParameterHandler.getParameter(index);
	}

	void setInternalAttribute(int index, float newValue) override
	{
		if (auto n = getActiveNetwork())
			n->networkParameterHandler.setParameter(index, newValue);
		else
			contentParameterHandler.setParameter(index, newValue);
	}

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		if (auto n = getActiveNetwork())
			return n->networkParameterHandler.getParameterId(parameterIndex);
		else
			return contentParameterHandler.getParameterId(parameterIndex);
	}

	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	void handleHiseEvent(const HiseEvent &m) override;
	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void calculateBlock(int startSample, int numSamples) override;;

	float startVoice(int voiceIndex) override;
	void stopVoice(int voiceIndex) override;
	void reset(int voiceIndex) override;
	bool isPlaying(int voiceIndex) const override;

	Processor *getChildProcessor(int /*processorIndex*/) override final { return nullptr; };
	const Processor *getChildProcessor(int /*processorIndex*/) const override final { return nullptr; };
	int getNumChildProcessors() const override final { return 0; };

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return Callback::numCallbacks; }
	void registerApiClasses() override;

	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

	void postCompileCallback() override;

private:

	int currentVoiceIndex = -1;

	struct ScriptEnvelopeState : public EnvelopeModulator::ModulatorState
	{
		ScriptEnvelopeState(int voiceIndex_) :
			EnvelopeModulator::ModulatorState(voiceIndex_)
		{};

		float uptime = 0.0f;
		bool isPlaying = false;
		bool isRingingOff = false;
		HiseEvent noteOnEvent;
	};

	struct VoiceData
	{
		bool operator==(const VoiceData& other) const
		{
			return other.voiceIndex == voiceIndex && noteOn == other.noteOn;
		}

		int voiceIndex;
		HiseEvent noteOn;
	};

	UnorderedStack<VoiceData, NUM_POLYPHONIC_VOICES> voiceNoteOns;
	
	HiseEvent lastNoteOn;
	

	ModulatorState *createSubclassedState(int voiceIndex) const override { return new ScriptEnvelopeState(voiceIndex); };

	ReferenceCountedObjectPtr<ScriptingApi::Message> currentMidiMessage;
	ReferenceCountedObjectPtr<ScriptingApi::Engine> engineObject;
	ScriptingApi::Synth *synthObject;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JavascriptEnvelopeModulator)
};

class JavascriptModulatorSynth : public JavascriptProcessor,
								 public ProcessorWithScriptingContent,
								 public ModulatorSynth
{
public:


	SET_PROCESSOR_NAME("ScriptSynth", "Script Synthesiser", "deprecated");

	enum class EditorStates
	{
		script1ChainShown = ModulatorSynth::numEditorStates,
		script2ChainShown,
		contentShown,
		onInitShown,
		prepareToPlayOpen,
		startVoiceOpen,
		renderVoiceOpen,
		onNoteOnOpen,
		onNoteOffOpen,
		onControllerOpen,
		onControlOpen,
		externalPopupShown,
		numEditorStates
	};

	enum InternalChains
	{
		ScriptChain1 = ModulatorSynth::numInternalChains,
		ScriptChain2,
		numInternalChains
	};

	enum class Callback
	{
		onInit = 0,
		prepareToPlay,
		startVoice,
		renderVoice,
		onNoteOn,
		onNoteOff,
		onController,
		onControl,
		numCallbacks
	};

	JavascriptModulatorSynth(MainController *mc, const String &id, int numVoices);
	~JavascriptModulatorSynth();

	void restoreFromValueTree(const ValueTree &v) override { ModulatorSynth::restoreFromValueTree(v); restoreScript(v); restoreContent(v); };
	ValueTree exportAsValueTree() const override { ValueTree v = ModulatorSynth::exportAsValueTree(); saveContent(v); saveScript(v); return v; }

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		return getContentParameterIdentifier(parameterIndex);
	}

	int getNumChildProcessors() const override { return numInternalChains; };
	int getNumInternalChains() const override { return numInternalChains; };

	virtual Processor *getChildProcessor(int processorIndex) override;;
	virtual const Processor *getChildProcessor(int processorIndex) const override;;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void preHiseEventCallback(const HiseEvent &m) override;
	void preStartVoice(int voiceIndex, const HiseEvent& e) override;;
	float getAttribute(int parameterIndex) const override;;
	void setInternalAttribute(int parameterIndex, float newValue) override;;

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return (int)Callback::numCallbacks; }
	void registerApiClasses() override;
	
	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

	int getCallbackEditorStateOffset() const override { return (int)EditorStates::contentShown; }

	void postCompileCallback() override;

	ProcessorEditorBody* createEditor(ProcessorEditor *parentEditor) override;

private:

	class Sound;
	class Voice;

	AudioSampleBuffer scriptChain1Buffer, scriptChain2Buffer;

	ScopedPointer<ModulatorChain> scriptChain1, scriptChain2;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> prepareToPlayCallback;
	ScopedPointer<SnippetDocument> startVoiceCallback;
	ScopedPointer<SnippetDocument> renderVoiceCallback;
	ScopedPointer<SnippetDocument> onNoteOnCallback;
	ScopedPointer<SnippetDocument> onNoteOffCallback;
	ScopedPointer<SnippetDocument> onControllerCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	ReferenceCountedObjectPtr<ScriptingApi::Message> currentMidiMessage;
	ReferenceCountedObjectPtr<ScriptingApi::Engine> engineObject;
	ScriptingApi::Synth *synthObject;
};



class JavascriptMasterEffect : public JavascriptProcessor,
							   public ProcessorWithScriptingContent,
							   public MasterEffectProcessor
{
public:

	SET_PROCESSOR_NAME("ScriptFX", "Script FX", "A scriptable audio effect.");

	enum class Callback
	{
		onInit,
		prepareToPlay,
		processBlock,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		prepareToPlayOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		processBlockOpen,
		onControlOpen,	
		externalPopupShown,
		numScriptEditorStates
	};
	
	JavascriptMasterEffect(MainController *mc, const String &id);
	~JavascriptMasterEffect();
	

	Path getSpecialSymbol() const override;

	void connectionChanged() override;

	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return (int)Callback::numCallbacks; }
	void registerApiClasses() override;
	void postCompileCallback() override;


	bool hasTail() const override { return false; };

	Processor *getChildProcessor(int /*processorIndex*/) override { return nullptr; };
	const Processor *getChildProcessor(int /*processorIndex*/) const override { return nullptr; };

	int getNumInternalChains() const override { return 0; };
	int getNumChildProcessors() const override { return 0; };

	virtual void renderWholeBuffer(AudioSampleBuffer &buffer);;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;
	void applyEffect(AudioSampleBuffer &b, int startSample, int numSamples) override;

	float getAttribute(int index) const override 
	{ 
		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameter(index);
	}
	
	void setInternalAttribute(int index, float newValue) override 
	{ 
		getCurrentNetworkParameterHandler(&contentParameterHandler)->setParameter(index, newValue);
	}

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameterId(parameterIndex);
	}

	ValueTree exportAsValueTree() const override { ValueTree v = MasterEffectProcessor::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override { MasterEffectProcessor::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }

	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

private:

	var buffers[NUM_MAX_CHANNELS];

	Array<var> channels;

	var channelData;

	Array<int, DummyCriticalSection> channelIndexes;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> prepareToPlayCallback;
	ScopedPointer<SnippetDocument> processBlockCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	ScriptingApi::Engine* engineObject;

	JUCE_DECLARE_WEAK_REFERENCEABLE(JavascriptMasterEffect);
};

class JavascriptPolyphonicEffect : public JavascriptProcessor,
	public ProcessorWithScriptingContent,
	public VoiceEffectProcessor
{
public:

	SET_PROCESSOR_NAME("PolyScriptFX", "Polyphonic Script FX", "A polyphonic scriptable audio effect.");

	enum class Callback
	{
		onInit,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		onControlOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		externalPopupShown,
		numScriptEditorStates
	};

	JavascriptPolyphonicEffect(MainController *mc, const String &id, int numVoices);
	~JavascriptPolyphonicEffect();

	bool isPolyphonic() const override { return true; }

	Path getSpecialSymbol() const override;

	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return (int)Callback::numCallbacks; }
	void registerApiClasses() override;
	void postCompileCallback() override;

	bool hasTail() const override { return false; };

	Processor *getChildProcessor(int /*processorIndex*/) override { return nullptr; };
	const Processor *getChildProcessor(int /*processorIndex*/) const override { return nullptr; };

	int getNumInternalChains() const override { return 0; };
	int getNumChildProcessors() const override { return 0; };

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;

	ValueTree exportAsValueTree() const override { ValueTree v = VoiceEffectProcessor::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override { VoiceEffectProcessor::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }

	float getAttribute(int index) const override
	{
		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameter(index);
	}

	void setInternalAttribute(int index, float newValue) override
	{
		getCurrentNetworkParameterHandler(&contentParameterHandler)->setParameter(index, newValue);
	}

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameterId(parameterIndex);
	}

	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

	void renderVoice(int voiceIndex, AudioSampleBuffer &b, int startSample, int numSamples) final override;

	void startVoice(int voiceIndex, const HiseEvent& e) final override;

	void stopVoice(int ) final override {}

	void reset(int voiceIndex) final override;

	void handleHiseEvent(const HiseEvent &m) final override;;

	void applyEffect(int , AudioSampleBuffer &, int , int ) final override
	{
		jassertfalse;
	}

	void renderNextBlock(AudioSampleBuffer &, int , int ) final override
	{

	}

private:

	struct VoiceData
	{
		bool operator==(const VoiceData& other) const
		{
			return other.voiceIndex == voiceIndex && noteOn == other.noteOn;
		}

		int voiceIndex;
		HiseEvent noteOn;
	};

	UnorderedStack<VoiceData, NUM_POLYPHONIC_VOICES> voiceNoteOns;

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	ScriptingApi::Engine* engineObject;
};

class JavascriptSynthesiser : public JavascriptProcessor,
						  public ProcessorWithScriptingContent,
						  public ModulatorSynth
{
public:

	enum ModChains
	{
		Extra1 = 2,
		Extra2,
		numModChains
	};

	struct Sound : public ModulatorSynthSound
	{
		bool appliesToNote(int ) final override { return true; };
		bool appliesToChannel(int ) final override { return true; };
		bool appliesToVelocity(int ) final override { return true; };
	};

	struct Voice : public ModulatorSynthVoice
	{
		Voice(JavascriptSynthesiser* p) :
			ModulatorSynthVoice(p),
			synth(p)
		{}

		

		void calculateBlock(int startSample, int numSamples) override;

		void setVoiceStartDataForNextRenderCallback()
		{
			isVoiceStart = true;
		}

		JavascriptSynthesiser* synth;

		bool isVoiceStart = false;
	};
	
	SET_PROCESSOR_NAME("ScriptSynth", "Scriptnode Syntesiser", "A polyphonic scriptable synthesiser.");

	enum class Callback
	{
		onInit,
		onControl,
		numCallbacks
	};

	enum EditorStates
	{
		onControlOpen = ProcessorWithScriptingContent::EditorStates::numEditorStates,
		externalPopupShown,
		numScriptEditorStates
	};

	JavascriptSynthesiser(MainController *mc, const String &id, int numVoices);
		
	~JavascriptSynthesiser();

	Path getSpecialSymbol() const override;

	ProcessorEditorBody *createEditor(ProcessorEditor *parentEditor)  override;

	SnippetDocument *getSnippet(int c) override;
	const SnippetDocument *getSnippet(int c) const override;
	int getNumSnippets() const override { return (int)Callback::numCallbacks; }
	void registerApiClasses() override;
	void postCompileCallback() override;

	void preHiseEventCallback(const HiseEvent &e) override;

	void preStartVoice(int voiceIndex, const HiseEvent& e) override;

	void prepareToPlay(double sampleRate, int samplesPerBlock) override;

	bool isPolyphonic() const override { return true; }

	float getModValueForNode(int modIndex, int startSample) const
	{
		if (modIndex == BasicChains::PitchChain)
		{
			auto& pc = modChains[BasicChains::PitchChain];
			if (auto pValues = pc.getReadPointerForVoiceValues(0))
				return pValues[startSample];
			else
				return pc.getConstantModulationValue();
		}
		else
		{
			return modChains[modIndex].getOneModulationValue(startSample);
		}
		
	}

	float getModValueAtVoiceStart(int modIndex) const;

	

	Processor* getChildProcessor(int processorIndex) override
	{
		if (processorIndex < ModulatorSynth::numInternalChains)
			return ModulatorSynth::getChildProcessor(processorIndex);
		if (processorIndex == ModulatorSynth::numInternalChains)
			return modChains[Extra1].getChain();
		if (processorIndex == ModulatorSynth::numInternalChains + 1)
			return modChains[Extra2].getChain();

		return nullptr;
	}

	const Processor* getChildProcessor(int processorIndex) const override
	{
		return const_cast<JavascriptSynthesiser*>(this)->getChildProcessor(processorIndex);
	}

	int getNumInternalChains() const override { return ModulatorSynth::numInternalChains + 2; };

	int getNumChildProcessors() const override { return getNumInternalChains(); };

	ValueTree exportAsValueTree() const override { ValueTree v = ModulatorSynth::exportAsValueTree(); saveContent(v); saveScript(v); return v; }
	void restoreFromValueTree(const ValueTree &v) override
	{ ModulatorSynth::restoreFromValueTree(v); restoreScript(v); restoreContent(v); }

	float getAttribute(int index) const override
	{
		if (index < ModulatorSynth::Parameters::numModulatorSynthParameters)
		{
			return ModulatorSynth::getAttribute(index);
		}

		index -= ModulatorSynth::Parameters::numModulatorSynthParameters;

		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameter(index);
	}

	void setInternalAttribute(int index, float newValue) override
	{
		if (index < ModulatorSynth::Parameters::numModulatorSynthParameters)
		{
			ModulatorSynth::setInternalAttribute(index, newValue);
			return;
		}

		index -= ModulatorSynth::Parameters::numModulatorSynthParameters;

		getCurrentNetworkParameterHandler(&contentParameterHandler)->setParameter(index, newValue);
	}

	Identifier getIdentifierForParameterIndex(int parameterIndex) const override
	{
		if (parameterIndex < ModulatorSynth::Parameters::numModulatorSynthParameters)
		{
			return ModulatorSynth::getIdentifierForParameterIndex(parameterIndex);
		}

		parameterIndex -= ModulatorSynth::Parameters::numModulatorSynthParameters;

		return getCurrentNetworkParameterHandler(&contentParameterHandler)->getParameterId(parameterIndex);
	}

	int getControlCallbackIndex() const override { return (int)Callback::onControl; };

	ModulatorChain::ModChainWithBuffer* nodeChains[3];

	ScopedPointer<SnippetDocument> onInitCallback;
	ScopedPointer<SnippetDocument> onControlCallback;

	ScriptingApi::Engine* engineObject;

	int currentVoiceStartSample = 0;

	JUCE_DECLARE_WEAK_REFERENCEABLE(JavascriptSynthesiser);
};


} // namespace hise
#endif  // SCRIPTPROCESSORMODULES_H_INCLUDED
