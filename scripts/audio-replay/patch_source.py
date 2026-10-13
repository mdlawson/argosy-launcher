import pathlib
import sys


root = pathlib.Path(sys.argv[1])


def replace_once(path, old, new):
    content = path.read_text()
    if content.count(old) != 1:
        raise RuntimeError(f"Expected source revision did not match: {path}")
    path.write_text(content.replace(old, new))


replace_once(root / "apu/bapu/dsp/sdsp.hpp",
             "#include <stdio.h>",
             "#include <stdio.h>\n\nvoid trace_dsp_write(uint8 address, uint8 data);")
replace_once(root / "apu/bapu/dsp/sdsp.hpp",
             "    spc_dsp.write(addr, data);",
             "    trace_dsp_write(addr, data);\n    spc_dsp.write(addr, data);")
replace_once(root / "apu/bapu/dsp/SPC_DSP.cpp",
             "\tint amp = (m.t_output * (int8_t) VREG(v->regs,voll + ch)) >> 7;",
             "\tint volume_amp(int output, int volume, int voice, int channel);\n"
             "\tint amp = volume_amp(m.t_output, (int8_t) VREG(v->regs,voll + ch), v->voice_number, ch);")
print("Patched the trace hook and optional voice-volume ramp.")
