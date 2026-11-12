"""Compile and exercise the actual interpolation methods without Qt/OBS SDKs."""
from pathlib import Path
import subprocess
source = Path(__file__).resolve().parents[1] / 'src/ptz-sequence.cpp'
text = source.read_text()
methods = text[text.index('int PTZSeqStep::keyframeDuration()'):text.index('QJsonObject PTZSeqStep::toJson()')]
preamble = r'''
#include <algorithm>
#include <vector>
#include <cassert>
#include <cmath>
enum class PTZKeyInterpolation { Linear, Smooth, Constant };
enum class PTZStepType { Wait, MoveRamp, Keyframes };
struct PTZKeyframe { int timeMs=0; double pan=0,tilt=0,zoom=0; PTZKeyInterpolation interpolation=PTZKeyInterpolation::Smooth; };
struct Keys : std::vector<PTZKeyframe> {
using std::vector<PTZKeyframe>::vector;
bool isEmpty() const { return empty(); }
const PTZKeyframe &first() const { return front(); }
const PTZKeyframe &last() const { return back(); }
};
struct PTZSeqStep {
Keys keyframes;
int delayMs=0, durationMs=1000;
PTZStepType type=PTZStepType::Keyframes;
int keyframeDuration() const;
PTZKeyframe sampleKeyframes(int) const;
int totalMs() const;
};
'''
tests = r'''
int main() {
PTZSeqStep s;
assert(s.keyframeDuration()==0);
assert(s.sampleKeyframes(500).pan==0);
s.keyframes={{0,-1,1,0,PTZKeyInterpolation::Linear},{1000,1,-1,1,PTZKeyInterpolation::Smooth}};
assert(s.keyframeDuration()==1000);
assert(s.sampleKeyframes(-100).pan==-1);
assert(s.sampleKeyframes(0).pan==-1);
assert(s.sampleKeyframes(500).pan==0);
assert(s.sampleKeyframes(500).zoom==.5);
assert(s.sampleKeyframes(1000).pan==1);
assert(s.sampleKeyframes(2000).pan==1);
s.keyframes[0].interpolation=PTZKeyInterpolation::Smooth;
assert(std::abs(s.sampleKeyframes(250).zoom-.15625)<1e-9);
s.keyframes[0].interpolation=PTZKeyInterpolation::Constant;
assert(s.sampleKeyframes(999).pan==-1);
assert(s.sampleKeyframes(1000).pan==1);
s.delayMs=200; assert(s.totalMs()==1200);
s.keyframes={{400, .2,.3,.4,PTZKeyInterpolation::Smooth}};
assert(s.sampleKeyframes(0).pan==.2);
assert(s.sampleKeyframes(999).pan==.2);
assert(s.totalMs()==600);
s.type=PTZStepType::MoveRamp; assert(s.totalMs()==1200);
s.type=PTZStepType::Wait; assert(s.totalMs()==1200);
}
'''
build = Path('/tmp/ptz-keyframe-tests'); build.mkdir(exist_ok=True)
(build/'test.cpp').write_text(preamble+methods+tests)
subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-sign-compare',str(build/'test.cpp'),'-o',str(build/'test')],check=True)
subprocess.run([str(build/'test')],check=True)
print('PASS: interpolation, boundary values, empty/single-key clips, duration, legacy timed steps')
