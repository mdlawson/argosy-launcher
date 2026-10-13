import hashlib
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import textwrap


helper = pathlib.Path(sys.argv[1]).resolve()
mock_source = r'''
import hashlib,json,os,pathlib,shlex,shutil,sys
root=pathlib.Path(os.environ["ARGOSY_MOCK_ADB_ROOT"])
args=sys.argv[1:]
with (root/"commands.jsonl").open("a") as log:log.write(json.dumps(args)+"\n")
def path(name):
    return root/("shared/"+name.lstrip("/") if name.startswith("/") else "app/"+name)
def fail():sys.exit(1)
if args==["get-state"]:print("device");sys.exit(0)
if args[:1]==["push"]:
    dest=path(args[2]);dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_bytes(pathlib.Path(args[1]).read_bytes()+(b"corrupt" if os.environ.get("ARGOSY_MOCK_CORRUPT") else b""));sys.exit(0)
if args[:1]!=["shell"]:fail()
cmd=shlex.split(" ".join(args[1:]))
if cmd==["getprop","ro.product.cpu.abilist"]:print("arm64-v8a,armeabi-v7a");sys.exit(0)
if cmd[:1]==["am"]:sys.exit(0)
if cmd[:1]==["run-as"]:
    if cmd[1]!="com.nendo.argosy.debug":fail()
    cmd=cmd[2:]
if cmd[:2]==["sh","-c"]:
    inner=shlex.split(cmd[2])
    if len(inner)!=4 or inner[0]!="echo" or inner[2]!=">":fail()
    path(inner[3]).write_text(inner[1]+"\n");sys.exit(0)
if cmd[0]=="test":
    p=path(cmd[2]);ok={"-f":p.is_file(),"-d":p.is_dir(),"-s":p.is_file() and p.stat().st_size>0}[cmd[1]]
    sys.exit(0 if ok else 1)
if cmd[0]=="sha256sum":
    p=path(cmd[1])
    if not p.is_file():fail()
    print(hashlib.sha256(p.read_bytes()).hexdigest()+"  "+cmd[1]);sys.exit(0)
if cmd[0]=="cat":
    p=path(cmd[1])
    if not p.is_file():fail()
    sys.stdout.write(p.read_text());sys.exit(0)
if cmd[0]=="cp":
    p=path(cmd[1]);dest=path(cmd[2])
    if not p.is_file():fail()
    dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest);sys.exit(0)
if cmd[0]=="mv":path(cmd[1]).replace(path(cmd[2]));sys.exit(0)
if cmd[0]=="chmod":sys.exit(0)
if cmd[:2]==["rm","-f"]:path(cmd[2]).unlink(missing_ok=True);sys.exit(0)
fail()
'''

with tempfile.TemporaryDirectory(prefix="argosy-core-helper-") as folder:
    root = pathlib.Path(folder)
    package = root / "package"
    package.mkdir()
    script = package / helper.name
    script.write_text(helper.read_text())
    replacement = b"test replacement core"
    (package / "snes9x_libretro_android.so").write_bytes(replacement)
    (package / "SHA256SUMS").write_text(hashlib.sha256(replacement).hexdigest() + "  snes9x_libretro_android.so\n")
    mock_bin = root / "bin"
    mock_bin.mkdir()
    adb = mock_bin / "adb"
    adb.write_text("#!" + sys.executable + "\n" + textwrap.dedent(mock_source))
    adb.chmod(0o755)
    device = root / "device"
    core = device / "app/files/libretro/cores/snes9x_libretro_android.so"
    core.parent.mkdir(parents=True)
    stock = b"test original core"
    core.write_bytes(stock)
    backup = core.with_name(core.name + ".pre-volume-smoothing")
    env = dict(os.environ, ARGOSY_MOCK_ADB_ROOT=str(device))
    env["PATH"] = str(mock_bin) + os.pathsep + env["PATH"]

    def run(mode, success=True, corrupt=False):
        options = dict(env)
        if corrupt:
            options["ARGOSY_MOCK_CORRUPT"] = "1"
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(script), "-Mode", mode],
                                input="\n", text=True, capture_output=True, env=options)
        assert (result.returncode == 0) == success, result.stdout + result.stderr

    run("smoothed")
    assert core.read_bytes() == replacement and backup.read_bytes() == stock
    commands = [json.loads(line) for line in (device / "commands.jsonl").read_text().splitlines()]
    stop = next(i for i, cmd in enumerate(commands) if cmd[:3] == ["shell", "am", "force-stop"])
    install = next(i for i, cmd in enumerate(commands) if len(cmd) > 3 and cmd[3] == "mv" and cmd[-1] == "files/libretro/cores/snes9x_libretro_android.so")
    assert stop < install
    run("smoothed")
    assert backup.read_bytes() == stock
    run("original")
    assert core.read_bytes() == stock and backup.read_bytes() == stock
    run("smoothed", success=False, corrupt=True)
    assert core.read_bytes() == stock and backup.read_bytes() == stock
    backup.write_bytes(b"damaged backup")
    run("original", success=False)
    assert core.read_bytes() == stock and backup.read_bytes() == b"damaged backup"
print("PASS: stop before replacement, verified backup, repeat installation, restoration, corrupt transfer, corrupt backup")
