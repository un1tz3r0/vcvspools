#include "length.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spools {

double Length::frames(double sr, double bpm) const {
	switch (unit) {
		case CropSpec::Seconds: return value * sr;
		case CropSpec::Beats: return value * 60.0 / std::max(bpm, 1e-6) * sr;
		default: return value;
	}
}

std::optional<Length> parseLength(const std::string& text, CropSpec::Unit def, int beatsPerBar) {
	const char* s = text.c_str();
	char* end = nullptr;
	double v = std::strtod(s, &end);
	if (end == s || !std::isfinite(v)) return std::nullopt;
	std::string unit;
	for (const char* p = end; *p; p++)
		if (!std::isspace((unsigned char)*p)) unit += char(std::tolower((unsigned char)*p));
	Length l{def, v};
	if (unit.empty()) return l;
	if (unit == "s" || unit == "sec" || unit == "secs" || unit == "seconds") l.unit = CropSpec::Seconds;
	else if (unit == "ms") l = {CropSpec::Seconds, v / 1000};
	else if (unit == "b" || unit == "beat" || unit == "beats") l.unit = CropSpec::Beats;
	else if (unit == "bar" || unit == "bars") l = {CropSpec::Beats, v * beatsPerBar};
	else if (unit == "smp" || unit == "samples" || unit == "frames") l.unit = CropSpec::Samples;
	else return std::nullopt;
	return l;
}

std::string formatLength(size_t frames, CropSpec::Unit unit, double sr, double bpm) {
	char buf[48];
	switch (unit) {
		case CropSpec::Seconds: std::snprintf(buf, sizeof buf, "%.4g s", frames / sr); break;
		case CropSpec::Beats: std::snprintf(buf, sizeof buf, "%.4g b", frames / sr * bpm / 60.0); break;
		default: std::snprintf(buf, sizeof buf, "%zu", frames);
	}
	return buf;
}

}
