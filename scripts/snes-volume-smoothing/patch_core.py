import pathlib
import shutil
import sys


root = pathlib.Path(sys.argv[1])
here = pathlib.Path(__file__).resolve().parent


def replace_once(path, old, new):
    content = path.read_text()
    if content.count(old) != 1:
        raise RuntimeError(f"Expected source revision did not match: {path}")
    path.write_text(content.replace(old, new))


shutil.copyfile(here / "VolumeRamp.h", root / "apu/bapu/dsp/VolumeRamp.h")
replace_once(root / "apu/bapu/dsp/SPC_DSP.h",
             '#include "blargg_common.h"',
             '#include "blargg_common.h"\n#include "VolumeRamp.h"')
replace_once(root / "apu/bapu/dsp/SPC_DSP.h",
             '\tvoid copy_state( unsigned char** io, copy_func_t );',
             '\tvoid copy_state( unsigned char** io, copy_func_t );\n\tvoid reset_volume_ramps();')
replace_once(root / "apu/bapu/dsp/SPC_DSP.h",
             '\tstruct state_t',
             '\tCoreVolumeRamp volume_ramps[voice_count][2];\n\n\tstruct state_t')
replace_once(root / "apu/bapu/dsp/SPC_DSP.cpp",
             '\tint amp = (m.t_output * (int8_t) VREG(v->regs,voll + ch)) >> 7;',
             '\tint amp = volume_ramps[v->voice_number][ch].scale(m.t_output, (int8_t) VREG(v->regs,voll + ch));')
replace_once(root / "apu/bapu/dsp/SPC_DSP.cpp",
             'void SPC_DSP::soft_reset_common()',
             'void SPC_DSP::reset_volume_ramps()\n{\n'
             '\tfor (int voice = 0; voice < voice_count; ++voice)\n'
             '\t\tfor (int channel = 0; channel < 2; ++channel)\n'
             '\t\t\tvolume_ramps[voice][channel].reset((int8_t) m.regs[voice * 16 + channel]);\n'
             '}\n\nvoid SPC_DSP::soft_reset_common()')
replace_once(root / "apu/bapu/dsp/SPC_DSP.cpp",
             '\tfor (int i = 0; i < voice_count; i++)\n\t\tm.voices[i].voice_number = i;',
             '\tfor (int i = 0; i < voice_count; i++)\n\t\tm.voices[i].voice_number = i;\n\treset_volume_ramps();')
replace_once(root / "apu/bapu/dsp/sdsp.cpp",
             '\tspc_dsp.copy_state(ptr, to_dsp_from_state);',
             '\tspc_dsp.copy_state(ptr, to_dsp_from_state);\n\tspc_dsp.reset_volume_ramps();')
replace_once(root / "libretro/libretro.cpp",
             '    info->library_version = VERSION GIT_VERSION;',
             '    info->library_version = VERSION " fae2fea volume-smooth64-test";')
print("Patched 64-sample voice-volume ramps, reset/load handling, and test-core identification.")
