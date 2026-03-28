#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <vector>

static constexpr std::array<int, 14> kDepthMenuSteps = {
	0, 5, 10, 15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100
};

struct NoiseVCO : Module {
	static constexpr int kMaxWavetableSize = 4096;
	static constexpr int kMaxVoices = 10; // 1 + unison(0..9)
	static constexpr std::array<int, 5> kWtSizeChoices = {256, 512, 1024, 2048, 4096};

	enum ParamIds {
		PITCH_PARAM,
		DETUNE_PARAM,
		UNISON_PARAM,
		OCTAVE_PARAM,
		MORPH_PARAM,
		WT_SIZE_PARAM,
		GEN_PARAM,

		MORPH_CV_DEPTH_PARAM,
		WT_SIZE_CV_DEPTH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VOCT_INPUT,
		TRIG_GEN_INPUT,
		MORPH_CV_INPUT,
		WT_SIZE_CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		LEFT_OUTPUT,
		RIGHT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		GEN_LIGHT,
		MORPH_MOD_LIGHT,
		WT_SIZE_MOD_LIGHT,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger genButtonTrigger;
	dsp::SchmittTrigger genInputTrigger;
	dsp::PulseGenerator genLightPulse;
	std::mt19937 rng {0x4e565f43u};

	std::array<float, kMaxWavetableSize> wtA {};
	std::array<float, kMaxWavetableSize> wtB {};
	std::array<float, kMaxWavetableSize> wtMorph {};
	int wtSize = 1024;
	float lastMorph = -1.f;

	std::array<float, kMaxVoices> phase {};

	mutable std::mutex wtMutex;

	NoiseVCO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configInput(VOCT_INPUT, "V/Oct");
		configInput(TRIG_GEN_INPUT, "Trigger GEN");
		configInput(MORPH_CV_INPUT, "Morph CV");
		configInput(WT_SIZE_CV_INPUT, "WT Size CV");

		configOutput(LEFT_OUTPUT, "Left");
		configOutput(RIGHT_OUTPUT, "Right");

		configParam(PITCH_PARAM, -4.f, 4.f, 0.f, "Pitch", " oct");
		configParam(DETUNE_PARAM, 0.f, 1.f, 0.12f, "Detune");
		auto* unisonQ = configParam(UNISON_PARAM, 0.f, 9.f, 0.f, "Unison");
		unisonQ->snapEnabled = true;
		auto* octaveQ = configParam(OCTAVE_PARAM, -3.f, 3.f, 0.f, "Octave shift", " oct");
		octaveQ->snapEnabled = true;
		configParam(MORPH_PARAM, 0.f, 1.f, 0.5f, "Morph");
		configSwitch(WT_SIZE_PARAM, 0.f, 4.f, 2.f, "WT size", {"256", "512", "1024", "2048", "4096"});
		configButton(GEN_PARAM, "GEN");

		configParam(MORPH_CV_DEPTH_PARAM, 0.f, 1.f, 1.f, "Morph CV depth", "%", 0.f, 100.f);
		configParam(WT_SIZE_CV_DEPTH_PARAM, 0.f, 1.f, 1.f, "WT Size CV depth", "%", 0.f, 100.f);

		regenerateWavePair();
		rebuildMorphTable(computeMorphParam());
	}

	float readMorphSample(float ph) const {
		int sizeLocal = wtSize;
		if (sizeLocal < 2) {
			return 0.f;
		}

		float pos = ph * static_cast<float>(sizeLocal - 1);
		int i0 = static_cast<int>(std::floor(pos));
		int i1 = i0 + 1;
		if (i1 >= sizeLocal) {
			i1 = 0;
		}
		float frac = pos - static_cast<float>(i0);
		return wtMorph[i0] + (wtMorph[i1] - wtMorph[i0]) * frac;
	}

	float getModulatedKnobValue(float baseValue, int cvInputId, int depthParamId, float minV, float maxV) {
		float depth = clamp(params[depthParamId].getValue(), 0.f, 1.f);
		float v = baseValue;
		if (inputs[cvInputId].isConnected()) {
			float cvNorm = clamp(inputs[cvInputId].getVoltage() / 5.f, -1.f, 1.f);
			float halfRange = 0.5f * (maxV - minV) * depth;
			v += cvNorm * halfRange;
		}
		return clamp(v, minV, maxV);
	}

	float computeMorphParam() {
		return getModulatedKnobValue(params[MORPH_PARAM].getValue(), MORPH_CV_INPUT, MORPH_CV_DEPTH_PARAM, 0.f, 1.f);
	}

	int computeWavetableSize() {
		float baseIdx = params[WT_SIZE_PARAM].getValue();
		float depth = clamp(params[WT_SIZE_CV_DEPTH_PARAM].getValue(), 0.f, 1.f);

		if (inputs[WT_SIZE_CV_INPUT].isConnected()) {
			float cvNorm = clamp(inputs[WT_SIZE_CV_INPUT].getVoltage() / 5.f, -1.f, 1.f);
			baseIdx += cvNorm * 2.f * depth; // depth 100% can sweep full 0..4 around center
		}

		int idx = clamp(static_cast<int>(std::round(baseIdx)), 0, static_cast<int>(kWtSizeChoices.size() - 1));
		return kWtSizeChoices[idx];
	}

	void generateNoiseWindowedWavetable(std::array<float, kMaxWavetableSize>& outTable, int size) {
		const int overscan = 4;
		const int noiseLen = std::max(size * overscan, size + 2);
		std::uniform_real_distribution<float> dist(-1.f, 1.f);

		std::vector<float> noise;
		noise.reserve(noiseLen);
		for (int i = 0; i < noiseLen; ++i) {
			noise.push_back(dist(rng));
		}

		std::vector<int> crossings;
		crossings.reserve(noiseLen / 4);
		for (int i = 0; i < noiseLen - 1; ++i) {
			float a = noise[i];
			float b = noise[i + 1];
			if ((a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f)) {
				crossings.push_back(i);
			}
		}

		int start = 0;
		int end = std::min(noiseLen - 1, size);
		if (crossings.size() >= 2) {
			start = crossings.front();
			int preferredLen = size;
			int minLen = std::max(8, size / 2);
			int bestEnd = -1;
			int bestScore = std::numeric_limits<int>::max();
			for (int c : crossings) {
				int len = c - start;
				if (len < minLen) {
					continue;
				}
				int score = std::abs(len - preferredLen);
				if (score < bestScore) {
					bestScore = score;
					bestEnd = c;
				}
			}
			if (bestEnd > start + 1) {
				end = bestEnd;
			}
		}

		int segmentLen = std::max(2, end - start);
		for (int i = 0; i < size; ++i) {
			float pos = (static_cast<float>(i) / static_cast<float>(size - 1)) * static_cast<float>(segmentLen - 1);
			int i0 = static_cast<int>(std::floor(pos));
			int i1 = std::min(i0 + 1, segmentLen - 1);
			float frac = pos - static_cast<float>(i0);
			int n0 = clamp(start + i0, 0, noiseLen - 1);
			int n1 = clamp(start + i1, 0, noiseLen - 1);
			outTable[i] = noise[n0] + (noise[n1] - noise[n0]) * frac;
		}

		float peak = 1e-6f;
		for (int i = 0; i < size; ++i) {
			peak = std::max(peak, std::abs(outTable[i]));
		}
		float invPeak = 1.f / peak;
		for (int i = 0; i < size; ++i) {
			outTable[i] *= invPeak;
		}
	}

	void regenerateWavePair() {
		std::array<float, kMaxWavetableSize> nextA {};
		std::array<float, kMaxWavetableSize> nextB {};
		int sizeLocal = wtSize;

		generateNoiseWindowedWavetable(nextA, sizeLocal);
		generateNoiseWindowedWavetable(nextB, sizeLocal);

		std::lock_guard<std::mutex> lock(wtMutex);
		for (int i = 0; i < sizeLocal; ++i) {
			wtA[i] = nextA[i];
			wtB[i] = nextB[i];
		}
	}

	void rebuildMorphTable(float morph) {
		int sizeLocal = wtSize;
		std::lock_guard<std::mutex> lock(wtMutex);
		for (int i = 0; i < sizeLocal; ++i) {
			wtMorph[i] = wtA[i] + (wtB[i] - wtA[i]) * morph;
		}
		lastMorph = morph;
	}

	void copyDisplayData(std::array<float, kMaxWavetableSize>& outData, int& outSize, float& outMorph) const {
		std::lock_guard<std::mutex> lock(wtMutex);
		outSize = wtSize;
		for (int i = 0; i < outSize; ++i) {
			outData[i] = wtMorph[i];
		}
		outMorph = lastMorph;
	}

	void onReset() override {
		for (float& p : phase) {
			p = 0.f;
		}
		regenerateWavePair();
		rebuildMorphTable(0.5f);
	}

	void process(const ProcessArgs& args) override {
		bool genEvent = false;
		if (genButtonTrigger.process(params[GEN_PARAM].getValue())) {
			genEvent = true;
		}
		if (genInputTrigger.process(inputs[TRIG_GEN_INPUT].getVoltage())) {
			genEvent = true;
		}

		int newSize = computeWavetableSize();
		if (newSize != wtSize) {
			wtSize = newSize;
			genEvent = true;
		}

		if (genEvent) {
			regenerateWavePair();
			genLightPulse.trigger(0.08f);
		}

		float morph = computeMorphParam();
		if (std::abs(morph - lastMorph) > 1e-4f || genEvent) {
			rebuildMorphTable(morph);
		}

		float voct = inputs[VOCT_INPUT].getVoltage();
		float pitchOct = params[PITCH_PARAM].getValue() + params[OCTAVE_PARAM].getValue() + voct;
		int unison = clamp(static_cast<int>(std::round(params[UNISON_PARAM].getValue())), 0, 9);
		int voices = 1 + unison;
		float detuneOct = params[DETUNE_PARAM].getValue() * 0.12f;

		float outL = 0.f;
		float outR = 0.f;
		for (int v = 0; v < voices; ++v) {
			float spread = 0.f;
			if (voices > 1) {
				spread = (static_cast<float>(v) / static_cast<float>(voices - 1)) * 2.f - 1.f;
			}
			float voicePitch = pitchOct + spread * detuneOct;
			float freq = dsp::FREQ_C4 * std::pow(2.f, voicePitch);
			freq = clamp(freq, 0.f, 20000.f);
			phase[v] += freq * args.sampleTime;
			phase[v] -= std::floor(phase[v]);

			float s = readMorphSample(phase[v]);
			float pan = clamp(0.5f + 0.35f * spread, 0.f, 1.f);
			float gainL = std::sqrt(1.f - pan);
			float gainR = std::sqrt(pan);
			outL += s * gainL;
			outR += s * gainR;
		}

		float norm = 1.f / std::sqrt(static_cast<float>(voices));
		outL = clamp(outL * norm * 5.f, -10.f, 10.f);
		outR = clamp(outR * norm * 5.f, -10.f, 10.f);

		outputs[LEFT_OUTPUT].setChannels(1);
		outputs[RIGHT_OUTPUT].setChannels(1);
		outputs[LEFT_OUTPUT].setVoltage(outL);
		outputs[RIGHT_OUTPUT].setVoltage(outR);

		float genLit = genLightPulse.process(args.sampleTime) ? 1.f : 0.f;
		lights[GEN_LIGHT].setBrightness(genLit);

		float morphMod = inputs[MORPH_CV_INPUT].isConnected() ? params[MORPH_CV_DEPTH_PARAM].getValue() : 0.f;
		float sizeMod = inputs[WT_SIZE_CV_INPUT].isConnected() ? params[WT_SIZE_CV_DEPTH_PARAM].getValue() : 0.f;
		lights[MORPH_MOD_LIGHT].setBrightnessSmooth(morphMod, args.sampleTime * 12.f);
		lights[WT_SIZE_MOD_LIGHT].setBrightnessSmooth(sizeMod, args.sampleTime * 12.f);
	}
};

struct PanelLabel : TransparentWidget {
	std::string text;
	int fontSize = 8;
	int align = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
	NVGcolor color = nvgRGB(0x0f, 0x17, 0x2a);

	void draw(const DrawArgs& args) override {
		auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (!font) {
			return;
		}
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, static_cast<float>(fontSize));
		nvgFillColor(args.vg, color);
		nvgTextAlign(args.vg, align);
		nvgText(args.vg, 0.f, 0.f, text.c_str(), nullptr);
	}
};

struct WavetableDisplay : TransparentWidget {
	NoiseVCO* moduleRef = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.f, 0.f, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, nvgRGB(0x12, 0x1a, 0x2b));
		nvgFill(args.vg);

		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0.5f, 0.5f, box.size.x - 1.f, box.size.y - 1.f, 3.f);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStrokeColor(args.vg, nvgRGB(0x2e, 0x3a, 0x52));
		nvgStroke(args.vg);

		if (!moduleRef) {
			return;
		}

		std::array<float, NoiseVCO::kMaxWavetableSize> wt {};
		int size = 0;
		float morph = 0.f;
		moduleRef->copyDisplayData(wt, size, morph);
		if (size < 2) {
			return;
		}

		nvgBeginPath(args.vg);
		for (int i = 0; i < size; ++i) {
			float t = static_cast<float>(i) / static_cast<float>(size - 1);
			float x = t * (box.size.x - 8.f) + 4.f;
			float y = (0.5f - 0.42f * wt[i]) * (box.size.y - 10.f) + 5.f;
			if (i == 0) {
				nvgMoveTo(args.vg, x, y);
			}
			else {
				nvgLineTo(args.vg, x, y);
			}
		}
		nvgStrokeWidth(args.vg, 1.4f);
		nvgStrokeColor(args.vg, nvgRGB(0x7d, 0xd3, 0xfc));
		nvgStroke(args.vg);

		auto font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font) {
			nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, 9.f);
			nvgFillColor(args.vg, nvgRGB(0xbf, 0xdb, 0xfe));
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			std::string info = rack::string::f("WT %d  M %.2f", size, morph);
			nvgText(args.vg, 5.f, 4.f, info.c_str(), nullptr);
		}
	}
};

struct CvDepthKnob : RoundSmallBlackKnob {
	NoiseVCO* moduleRef = nullptr;
	int depthParam = -1;
	int cvInput = -1;
	std::string depthMenuLabel = "CV depth";

	void draw(const DrawArgs& args) override {
		RoundSmallBlackKnob::draw(args);
		if (!moduleRef || depthParam < 0 || cvInput < 0) {
			return;
		}
		if (!moduleRef->inputs[cvInput].isConnected()) {
			return;
		}

		auto* pq = getParamQuantity();
		if (!pq) {
			return;
		}

		float minV = pq->getMinValue();
		float maxV = pq->getMaxValue();
		float baseV = moduleRef->params[paramId].getValue();
		float modV = moduleRef->getModulatedKnobValue(baseV, cvInput, depthParam, minV, maxV);
		float depth = clamp(moduleRef->params[depthParam].getValue(), 0.f, 1.f);
		float halfRange = 0.5f * (maxV - minV) * depth;
		float lowV = clamp(baseV - halfRange, minV, maxV);
		float highV = clamp(baseV + halfRange, minV, maxV);

		auto normalize = [minV, maxV](float v) {
			if (maxV <= minV) {
				return 0.f;
			}
			return clamp((v - minV) / (maxV - minV), 0.f, 1.f);
		};

		const float pi = 3.14159265359f;
		const float ringMinA = minAngle - 0.5f * pi;
		const float ringMaxA = maxAngle - 0.5f * pi;
		auto toAngle = [&](float v) {
			float t = normalize(v);
			return ringMinA + t * (ringMaxA - ringMinA);
		};

		const Vec c = box.size.div(2.f);
		const float r = std::min(box.size.x, box.size.y) * 0.56f;

		auto drawArc = [&](float fromValue, float toValue, NVGcolor col, float width) {
			float a0 = toAngle(fromValue);
			float a1 = toAngle(toValue);
			if (a1 < a0) {
				std::swap(a0, a1);
			}
			nvgBeginPath(args.vg);
			nvgArc(args.vg, c.x, c.y, r, a0, a1, NVG_CW);
			nvgStrokeWidth(args.vg, width);
			nvgStrokeColor(args.vg, col);
			nvgStroke(args.vg);
		};

		drawArc(minV, maxV, nvgRGBA(71, 85, 105, 120), 1.2f);
		drawArc(lowV, highV, nvgRGBA(37, 99, 235, 220), 2.0f);

		auto drawTick = [&](float value, NVGcolor col, float width, float len) {
			float a = toAngle(value);
			float x0 = c.x + std::cos(a) * (r - len);
			float y0 = c.y + std::sin(a) * (r - len);
			float x1 = c.x + std::cos(a) * (r + 0.5f);
			float y1 = c.y + std::sin(a) * (r + 0.5f);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, x0, y0);
			nvgLineTo(args.vg, x1, y1);
			nvgStrokeWidth(args.vg, width);
			nvgStrokeColor(args.vg, col);
			nvgStroke(args.vg);
		};

		drawTick(baseV, nvgRGBA(15, 23, 42, 220), 1.5f, 4.2f);
		drawTick(modV, nvgRGBA(16, 185, 129, 240), 2.2f, 5.8f);
	}

	void appendContextMenu(Menu* menu) override {
		RoundSmallBlackKnob::appendContextMenu(menu);
		if (!moduleRef || depthParam < 0) {
			return;
		}

		int depthRounded = clamp(static_cast<int>(std::round(moduleRef->params[depthParam].getValue() * 100.f)), 0, 100);
		std::string rightText = rack::string::f("%d%%", depthRounded);

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem(depthMenuLabel, rightText, [this](Menu* submenu) {
			for (int pct : kDepthMenuSteps) {
				submenu->addChild(createCheckMenuItem(
					rack::string::f("%d%%", pct),
					"",
					[this, pct]() {
						int curr = clamp(static_cast<int>(std::round(moduleRef->params[depthParam].getValue() * 100.f)), 0, 100);
						return curr == pct;
					},
					[this, pct]() {
						moduleRef->params[depthParam].setValue(pct / 100.f);
					}
				));
			}
		}));
	}
};

struct NoiseVCOWidget : ModuleWidget {
	NoiseVCOWidget(NoiseVCO* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/NoiseVCO.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		auto* display = createWidget<WavetableDisplay>(mm2px(Vec(3.5f, 12.0f)));
		display->box.size = mm2px(Vec(54.0f, 24.0f));
		display->moduleRef = module;
		addChild(display);

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(12.0f, 47.0f)), module, NoiseVCO::PITCH_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(26.0f, 47.0f)), module, NoiseVCO::DETUNE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(40.0f, 47.0f)), module, NoiseVCO::UNISON_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(54.0f, 47.0f)), module, NoiseVCO::OCTAVE_PARAM));

		auto* morphKnob = createParamCentered<CvDepthKnob>(mm2px(Vec(20.0f, 63.0f)), module, NoiseVCO::MORPH_PARAM);
		morphKnob->moduleRef = module;
		morphKnob->depthParam = NoiseVCO::MORPH_CV_DEPTH_PARAM;
		morphKnob->cvInput = NoiseVCO::MORPH_CV_INPUT;
		morphKnob->depthMenuLabel = "MORPH CV depth";
		addParam(morphKnob);

		auto* sizeKnob = createParamCentered<CvDepthKnob>(mm2px(Vec(40.0f, 63.0f)), module, NoiseVCO::WT_SIZE_PARAM);
		sizeKnob->moduleRef = module;
		sizeKnob->depthParam = NoiseVCO::WT_SIZE_CV_DEPTH_PARAM;
		sizeKnob->cvInput = NoiseVCO::WT_SIZE_CV_INPUT;
		sizeKnob->depthMenuLabel = "WT SIZE CV depth";
		addParam(sizeKnob);

		addParam(createParamCentered<LEDButton>(mm2px(Vec(12.0f, 79.0f)), module, NoiseVCO::GEN_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(19.0f, 79.0f)), module, NoiseVCO::GEN_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(27.0f, 79.0f)), module, NoiseVCO::MORPH_MOD_LIGHT));
		addChild(createLightCentered<MediumLight<BlueLight>>(mm2px(Vec(35.0f, 79.0f)), module, NoiseVCO::WT_SIZE_MOD_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(10.0f, 97.0f)), module, NoiseVCO::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(24.0f, 97.0f)), module, NoiseVCO::TRIG_GEN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(38.0f, 97.0f)), module, NoiseVCO::MORPH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(52.0f, 97.0f)), module, NoiseVCO::WT_SIZE_CV_INPUT));

		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(20.0f, 114.0f)), module, NoiseVCO::LEFT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(42.0f, 114.0f)), module, NoiseVCO::RIGHT_OUTPUT));

		auto addPanelLabel = [this](float xMm, float yMm, const char* txt, int size = 8, NVGcolor color = nvgRGB(0x0f, 0x17, 0x2a)) {
			auto* l = createWidget<PanelLabel>(mm2px(Vec(xMm, yMm)));
			l->text = txt;
			l->fontSize = size;
			l->color = color;
			addChild(l);
		};

		addPanelLabel(30.5f, 7.5f, "NOISE VCO", 10, nvgRGB(0x0b, 0x12, 0x20));
		addPanelLabel(30.5f, 38.2f, "PITCH  DETUNE  UNISON  OCT", 7, nvgRGB(0x1f, 0x29, 0x37));
		addPanelLabel(20.0f, 54.2f, "MORPH", 8);
		addPanelLabel(40.0f, 54.2f, "WT SIZE", 8);
		addPanelLabel(12.0f, 71.2f, "GEN", 8);
		addPanelLabel(27.0f, 71.2f, "M", 8);
		addPanelLabel(35.0f, 71.2f, "S", 8);

		addPanelLabel(10.0f, 90.6f, "VOCT", 7);
		addPanelLabel(24.0f, 90.6f, "TRIG", 7);
		addPanelLabel(38.0f, 90.6f, "M CV", 7);
		addPanelLabel(52.0f, 90.6f, "S CV", 7);

		addPanelLabel(20.0f, 107.5f, "L OUT", 7);
		addPanelLabel(42.0f, 107.5f, "R OUT", 7);
	}
};

Model* modelNoiseVCO = createModel<NoiseVCO, NoiseVCOWidget>("NoiseVCO");
