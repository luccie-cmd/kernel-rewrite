#!/bin/python3
import os
import glob
import sys
import subprocess
import threading
from script.util import parseSize, compareFiles
import atexit

def readConfig(path: str) -> dict[str, list[str]]:
    """Reads a configuration file and returns a dictionary of lists of strings."""
    config = {}
    try:
        with open(path, "r") as f:
            lines = f.readlines()
            for line in lines:
                line = line.strip()
                if ':' in line:
                    key, value = line.split(":", 1)
                    key = key.strip()
                    value = value.strip().strip("'")
                    config[key] = [v.strip().strip("'") for v in value.split(",")]
    except FileNotFoundError:
        print(f"Error: Configuration file '{path}' not found.")
        exit(1)
    return config


def writeConfig(config: dict[str, list[str]], path: str) -> None:
    """Writes a configuration dictionary to a file."""
    with open(path, "w") as f:
        for key, values in config.items():
            f.write(f"{key}:")
            for val in enumerate(values):
                f.write(f"'{val[1]}'")
                if val[0]+1 < len(values):
                    f.write(", ")
            f.write('\n')

def checkConfig(config: dict[str, list[str]], allowed_config: list[list[str, list[str], bool]]) -> bool:
    for (key, valid_values, is_mandatory) in allowed_config:
        if key not in config:
            if is_mandatory:
                print(f"Mandatory config {key} is missing")
                return False
            continue
        if valid_values:
            for value in config[key]:
                if value not in valid_values:
                    print(f"Invalid value '{value}' for key '{key}'")
                    return False
    return True

CONFIG = readConfig("./script/config.py")
OLD_CONFIG = {}
if os.path.exists("./script/config.py.old"):
    OLD_CONFIG = readConfig("./script/config.py.old")
ALLOWED_CONFIG = [
    ["config", ["release", "debug"], True],
    ["arch", ["x64"], True],
    ["compiler", ["gcc", "clang", "custom"], True],
    ["rootFS", ["fat32", 'ext2', 'ext3', "ext4"], True],
    ["bootloader", ["limine-uefi", "custom"], True],
    ["outDir", [], True],
    ["analyzer", ["yes", "no"], True],
    ["imageSize", [], False],
    ["usan", ["yes", "no"], True],
    ["asan", ["yes", "no"], True],
]
if not checkConfig(CONFIG, ALLOWED_CONFIG):
    print("Invalid config file.")
    print("Allowed config items")
    for option in ALLOWED_CONFIG:
        name = option[0]
        values = option[1]
        required = option[2]
        print(f"{name} (required = {required})")
        if len(values) == 0:
            print("    This can be anything as long as it's provided")
        else:
            for val in values:
                print(f"  - {val}")
    exit(1)
writeConfig(CONFIG, "./script/config.py.old")
force_rebuild = False
if OLD_CONFIG != CONFIG:
    force_rebuild = True
    print("Configuration changed, rebuilding...")
CONFIG["CFLAGS"] = ['-c', '-nostdlib', '-DCOMPILE', '-fno-pie', '-fno-PIE', '-fno-pic', '-fno-PIC', '-fno-builtin', '-ggdb', '-D_LIBCPP_HAS_NO_THREADS']
CONFIG["CFLAGS"] += ['-fno-strict-aliasing', '-fno-stack-protector', '-fno-lto']
CONFIG["CFLAGS"] += ['-Werror', '-Wall', '-Wextra', '-Wpointer-arith', '-Wshadow', '-Wno-unused-function']
CONFIG["CFLAGS"] += ['-mno-red-zone', '-march=native', '-mtune=native', '-mcmodel=kernel', '-mno-tls-direct-seg-refs']
CONFIG["CXXFLAGS"] = ['-fno-exceptions', '-fno-rtti']
CONFIG["ASFLAGS"] = ['-felf64', '-g']
CONFIG["LDFLAGS"] = ['-Wl,--build-id=none', '-Wl,--hash-style=sysv', '-Wl,--no-undefined', '-Wl,-no-pie', '-Wl,--no-dynamic-linker' , '-nostdlib', '-ffunction-sections', '-fdata-sections', '-fno-pie', '-fno-PIE', '-fno-pic', '-fno-PIC', '-Oz', '-mcmodel=kernel', '-fno-lto']
CONFIG["INCPATHS"] = ['-Iinclude', '-I /usr/lib/gcc/x86_64-pc-linux-gnu/11.4.0/include/c++', '-I /usr/lib/gcc/x86_64-pc-linux-gnu/11.4.0/include/c++/x86_64-pc-linux-gnu', '-I /usr/lib/gcc/x86_64-pc-linux-gnu/11.4.0/include/c++/backward', '-I /usr/lib/gcc/x86_64-pc-linux-gnu/11.4.0/include', '-I /usr/local/include', '-I /usr/lib/gcc/x86_64-pc-linux-gnu/11.4.0/include-fixed', '-I /usr/include', '-I./', '-ILimine']
if "imageSize" not in CONFIG:
    CONFIG["imageSize"] = '128m'

if "debug" in CONFIG.get("config"):
    CONFIG["CFLAGS"] += ["-O0"]
    CONFIG["CFLAGS"] += ["-DDEBUG"]
else:
    CONFIG["CFLAGS"] += ["-O2"]
    CONFIG["CFLAGS"] += ["-DDEBUG"]
    # CONFIG["CFLAGS"] += ["-DNDEBUG"]

if "x64" in CONFIG.get("arch"):
    CONFIG["CFLAGS"] += ["-m64"]

if "yes" in CONFIG.get("usan"):
    CONFIG["CFLAGS"] += ['-fsanitize=undefined']
    if "clang" not in CONFIG.get("compiler"):
        CONFIG["LDFLAGS"] += ['-fsanitize=undefined']

if "yes" in CONFIG.get("asan"):
    CONFIG["CFLAGS"] += ['-fsanitize=kernel-address']
    if "clang" not in CONFIG.get("compiler"):
        CONFIG["LDFLAGS"] += ['-fsanitize=kernel-address']

if "gcc" in CONFIG.get("compiler"):
    if "yes" in CONFIG.get("analyzer"):
        CONFIG["CFLAGS"] += ["-fanalyzer", "-Wno-analyzer-malloc-leak"]
    CONFIG["CFLAGS"] += ['-fprefetch-loop-arrays', '-fmax-errors=1']
    CONFIG["CFLAGS"] += ['-Wno-aggressive-loop-optimizations']
if "clang" in CONFIG.get("compiler"):
    CONFIG["CFLAGS"] += ['-Wno-ignored-attributes']

CONFIG["CFLAGS"].append('-fno-omit-frame-pointer')

stopEvent = threading.Event()

def callCmd(command, print_out=False):
    with open("commands.txt", "a") as f:
        f.write(command+'\n')
    result = subprocess.run(command, capture_output=not print_out, text=True, shell=True)
    if result.returncode != 0:
        print(result.stderr)
    return [result.returncode, result.stdout]

callCmd("rm -rf commands.txt")

if not compareFiles('build.py', '.build-cache/build.py'):
    if not os.path.exists(".build-cache"):
        callCmd(f"mkdir -p .build-cache")
    callCmd(f"cp 'build.py' .build-cache/build.py")
    force_rebuild = True


def checkExtension(file: str, valid_extensions: list[str]):
    for ext in valid_extensions:
        if file.endswith(ext):
            return True
    return False

def getExtension(file):
    return file.split(".")[-1]

def buildC(file):
    compiler = CONFIG.get("compiler")[0]
    
    if compiler == "custom":
        compiler = "../cCompiler/bin/cCompiler.elf"
    # if CONFIG.get("compiler")[0] == "gcc":
    #     compiler += "-11"
    options = CONFIG["CFLAGS"].copy()
    options.append("-std=c23")
    command = compiler + " " + file
    for option in options:
        command += " " + option
    print(f"C     {file}")
    command += f" -o {CONFIG['outDir'][0]}/{file}.o"
    return callCmd(command, True)[0]
    
def buildCXX(file):
    compiler = CONFIG.get("compiler")[0]
    if compiler == "gcc":
        compiler = "g"
    compiler += "++"
    # if CONFIG.get("compiler")[0] == "gcc":
    #     compiler += "-11"
    options = CONFIG["CFLAGS"].copy()
    options += CONFIG["CXXFLAGS"].copy()
    options.append("-std=c++26")
    if "yes" in CONFIG.get("analyzer"):
        options.remove("-fanalyzer")
    command = compiler + " " + file
    for option in options:
        command += " " + option
    print(f"CXX   {file}")
    command += f" -o {CONFIG['outDir'][0]}/{file}.o"
    return callCmd(command, True)[0]

def buildASM(file):
    compiler = "nasm"
    options = CONFIG["ASFLAGS"].copy()
    command = compiler + " " + file
    for option in options:
        command += " " + option
    print(f"AS    {file}")
    command += f" -o {CONFIG['outDir'][0]}/{file}.o"
    return callCmd(command, True)[0]

def buildAR(dir: str, out_file: str):
    files = glob.glob(f"{dir}/**", recursive=True)
    obj_files = []
    for file in files:
        if not os.path.isfile(file):
            continue
        if not checkExtension(file, ["o", "bc"]):
            continue
        obj_files.append(file)
    obj_files_str = " ".join(obj_files)
    cmd = f"ar rcs {out_file} {obj_files_str}"
    print(f"AR    {out_file}")
    callCmd(cmd)

def buildSO(dir: str, out_file: str):
    files = glob.glob(f"{dir}/**", recursive=True)
    obj_files = []
    for file in files:
        if not os.path.isfile(file):
            continue
        if not checkExtension(file, ["o", "bc"]):
            continue
        obj_files.append(file)
    obj_files_str = " ".join(obj_files)
    cmd = f"gcc -shared -o {out_file} {obj_files_str} -nostdlib -fPIC -Wl,--hash-style=sysv"
    print(f"SO    {out_file}")
    callCmd(cmd)
    outName = os.path.basename(out_file)
    callCmd(f"objdump -C -D -x -Mintel -g -r -t -L {CONFIG['outDir'][0]}/{outName} > {CONFIG['outDir'][0]}/{outName}.asm")

def buildKernel(kernel_dir: str):
    files = glob.glob(kernel_dir+'/**', recursive=True)
    files = sorted(files, key=str.lower)
    CONFIG["INCPATHS"] += [f"-I{kernel_dir}"]
    for file in files:
        if not os.path.isfile(file):
            continue
        if not checkExtension(file, ["c", "cc", "asm"]):
            continue
        if getExtension(file) == "inc" or getExtension(file) == "h":
            continue
        basename = os.path.basename(os.path.dirname(os.path.realpath(__file__)))
        str_paths = ""
        for incPath in CONFIG["INCPATHS"]:
            str_paths += f" {incPath}"
        if "clang" in CONFIG["compiler"]:
            str_paths += ' -D__clang__'
        # if "debug" in CONFIG["config"]:
        str_paths += ' -DDEBUG'
        code, _ = callCmd(f"cpp {str_paths} -D_GLIBC_HOSTED=1 {file} -o ./tmp.txt", True)
        if code != 0:
            print(f"CPP failed to pre process {file}")
            exit(code)
        if not force_rebuild and compareFiles("./tmp.txt", os.path.abspath(f"./.build-cache/{basename}/cache/{file}")):
            continue
        callCmd(f"mkdir -p {CONFIG['outDir'][0]}/{os.path.dirname(file)}")
        callCmd(f"mkdir -p ./.build-cache/{basename}/cache/{os.path.dirname(file)}")
        callCmd(f"cp ./tmp.txt ./.build-cache/{basename}/cache/{file}")
        code = 0
        CONFIG["CFLAGS"] += CONFIG["INCPATHS"]
        CONFIG["ASFLAGS"] += CONFIG["INCPATHS"]
        if getExtension(file) == "cc" or getExtension(file) == "c":
            print(f"FMT   {file}")
            callCmd(f"clang-format -i {file}")
        if getExtension(file) == "c":
            code = buildC(file)
        elif getExtension(file) == "asm":
            code = buildASM(file)
        elif getExtension(file) == "cc":
            code = buildCXX(file)
        else:
            print(f"Invalid or unhandled extension `{getExtension(file)}` on file {file}")
            exit(1)

        for incPath in CONFIG["INCPATHS"]:
            CONFIG["CFLAGS"].remove(incPath)
            CONFIG["ASFLAGS"].remove(incPath)

        if code != 0:
            callCmd(f"rm -f ./.build-cache/{basename}/cache/{file}")
            exit(code)

def linkDir(kernel_dir, linker_file, outName, static_lib_files=[]):
    files = glob.glob(kernel_dir+'/**', recursive=True)
    if "gcc" in CONFIG["compiler"]:
        command = "g++"
    else:
        command = "clang++ -fuse-ld=lld"
    options = CONFIG["LDFLAGS"]
    for option in options:
        command += " " + option
    for file in files:
        if not os.path.isfile(file):
            continue
        if not checkExtension(file, ["o", "bc"]):
            continue
        command += " " + file
    if linker_file != None:
        command += f" -Wl,-T {linker_file}"
        command +=  " -Wl,--no-whole-archive"
        command +=  " -Wl,--whole-archive"
    for static_lib in static_lib_files:
        command += f" {static_lib}"
    command += f" -Wl,-Map={CONFIG['outDir'][0]}/{outName}.map"
    command += f" -o {CONFIG['outDir'][0]}/{outName}.elf"
    file = f"{CONFIG['outDir'][0]}/{outName}.elf"
    print(f"LD   {file}")
    if callCmd(command, True)[0] != 0:
        print(f"LD   {file} Failed")
        exit(1)
    callCmd(f"objdump -C -D -x -Mintel -g -r -t -L {CONFIG['outDir'][0]}/{outName}.elf > {CONFIG['outDir'][0]}/{outName}.asm")

def makeImageFile(out_file):
    size = parseSize(CONFIG["imageSize"][0])
    divSize = parseSize("1M")
    command = f"dd if=/dev/zero of={out_file} bs=1M count={size//divSize}"
    print("> Making image file")
    callCmd(command)

def makePartitionTable(out_file):
    print("> Making GPT partition")
    command = f"parted {out_file} --script mklabel gpt"
    callCmd(command)
    print("> Making EFI partition")
    command = f"parted {out_file} --script mkpart EFI FAT32 2048s 100MB"
    callCmd(command)
    print("> Making HOME partition")
    command = f"parted {out_file} --script mkpart HOME FAT32 100MB 100%"
    callCmd(command)
    print("> Setting EFI partition to be bootable")
    command = f"parted {out_file} --script set 1 boot on"
    callCmd(command)
    command = f"parted {out_file} --script set 2 boot on"
    callCmd(command)

def setupLoopDevice(out_file):
    print("> Setting up loop device")
    command = f"losetup --show -f -P {out_file} > ./.build-cache/tmp.txt"
    callCmd(command)
    loop_device = ""
    with open("./.build-cache/tmp.txt") as f:
        loop_device = f.readline()
    loop_device = loop_device.strip()
    print(f"> Loop device: {loop_device}")
    return loop_device

def makeFileSystem(loop_device):
    print("> Formatting file systems")
    callCmd(f"mkfs.fat -F32 {loop_device}p1")
    callCmd(f"mkfs.fat -F32 {loop_device}p2")
    # ONLY ADD THIS LINE BACK IN WHEN WE'VE GOT A WORKING FS DRIVER
    # callCmd(f"./mkfs.fs {loop_device}p2")

def mountFs(device, boot, kernel, initramFS, files):
    callCmd(f"mkdir -p mnt")
    print("> Copying boot files")
    callCmd(f"mount {device}p1 mnt")
    callCmd(f"mkdir -p mnt/EFI/BOOT")
    callCmd(f"cp {boot} mnt/EFI/BOOT")
    callCmd(f"cp {kernel} mnt")
    print("Creating mountFile")
    callCmd(f"rm -rf mountFile")
    callCmd(f"touch mountFile")
    callCmd(f"lsblk {device}p2 -pno PARTUUID | tr -d '-' | awk '{{print \"\" $1 \" /\"}}' >> mountFile")
    callCmd(f"lsblk {device}p1 -pno PARTUUID | tr -d '-' | awk '{{print \"\" $1 \" /boot\"}}' >> mountFile")
    print("Done creating mount file")
    buildInitImg()
    callCmd(f"cp {initramFS} mnt")
    if "limine-uefi" in CONFIG["bootloader"]:
        callCmd(f"cp {CONFIG['outDir'][0]}/limine.conf mnt")
        callCmd(f"cp {CONFIG['outDir'][0]}/limine-bios.sys mnt")
    callCmd(f"umount -l mnt")
    callCmd(f"mount {device}p2 mnt")
    callCmd(f"mkdir -p mnt/lib")
    callCmd(f"mkdir -p mnt/bin")
    callCmd(f"cp {CONFIG['outDir'][0]}/libc_init.so mnt/lib/libc_init.so")
    callCmd(f"cp {CONFIG['outDir'][0]}/shell.elf mnt/bin/init")
    for file in files:
        if os.path.isfile(file):
            callCmd(f"cp {file} mnt")
        else:
            callCmd(f"mkdir mnt/{file}")
    callCmd(f"umount -l mnt")
    callCmd(f"losetup -d {device}")
    callCmd(f"rm -rf mnt")

def buildInitImg():
    out_file = f"{CONFIG['outDir'][0]}/init.img"
    command = f"dd if=/dev/zero of={out_file} bs=1M count={10}"
    callCmd(command)
    print("> Making GPT partition")
    command = f"parted {out_file} --script mklabel gpt"
    callCmd(command)
    print("> Making DATA partition")
    command = f"parted {out_file} --script mkpart DATA FAT32 2048s 100%"
    callCmd(command)
    command = f"parted {out_file} --script set 1 boot on"
    callCmd(command)
    LOOP_DEVICE=setupLoopDevice(out_file)
    print("> Formatting file systems")
    callCmd(f"mkfs.fat -F32 {LOOP_DEVICE}p1")
    callCmd(f"mkdir -p mnt2")
    print("> Copying boot files")
    callCmd(f"mount {LOOP_DEVICE}p1 mnt2")
    callCmd(f"mkdir -p mnt2/lib")
    callCmd(f"cp ./bin/init.elf mnt2/init")
    callCmd(f"cp ./bin/libc_init.so mnt2/lib/libc_init.so")
    callCmd(f"cp ./mountFile ./mnt2/mount")
    callCmd(f"umount -l mnt2")
    callCmd(f"losetup -d {LOOP_DEVICE}")
    callCmd(f"rm -rf mnt2")

def buildImage(out_file, boot_file, kernel_file, initRam, files: list[tuple]):
    if force_rebuild or not os.path.exists(out_file):
        if not out_file.startswith("/dev"):
            callCmd(f"rm -f {out_file}")
            makeImageFile(out_file)
        makePartitionTable(out_file)
    LOOP_DEVICE=setupLoopDevice(out_file)
    makeFileSystem(LOOP_DEVICE)
    mountFs(LOOP_DEVICE, boot_file, kernel_file, initRam, files)
    if "limine-uefi" in CONFIG["bootloader"]:
        callCmd(f"./Limine/bin/limine bios-install --no-gpt-to-mbr-isohybrid-conversion {out_file}")

def buildStaticLib(directory, out_file):
    os.makedirs(CONFIG["outDir"][0]+'/'+directory, exist_ok=True)
    CONFIG["INCPATHS"] += [f'-I{directory}']
    files = glob.glob(directory+'/**', recursive=True)
    files = sorted(files, key=str.lower)
    for file in files:
        if not os.path.isfile(file):
            continue
        if not checkExtension(file, ["c", "cc", "asm"]):
            continue
        basename = os.path.basename(os.path.dirname(os.path.realpath(__file__)))
        str_paths = ""
        for incPath in CONFIG["INCPATHS"]:
            str_paths += f" {incPath}"
        if "clang" in CONFIG["compiler"]:
            str_paths += ' -D__clang__'
        # if "debug" in CONFIG["config"]:
        str_paths += ' -DDEBUG'
        code, _ = callCmd(f"cpp {str_paths} -D_GLIBC_HOSTED=1 {file} -o ./tmp.txt", True)
        if code != 0:
            print(f"CPP failed to pre process {file}")
            exit(code)
        if not force_rebuild and compareFiles("./tmp.txt", os.path.abspath(f"./.build-cache/{basename}/cache/{file}")):
            continue
        callCmd(f"mkdir -p {CONFIG['outDir'][0]}/{os.path.dirname(file)}")
        callCmd(f"mkdir -p ./.build-cache/{basename}/cache/{os.path.dirname(file)}")
        callCmd(f"cp ./tmp.txt ./.build-cache/{basename}/cache/{file}")
        code = 0
        CONFIG["CFLAGS"] += CONFIG["INCPATHS"]
        CONFIG["ASFLAGS"] += CONFIG["INCPATHS"]
        if getExtension(file) == "cc" or getExtension(file) == "c":
            print(f"FMT   {file}")
            callCmd(f"clang-format -i {file}")
        if getExtension(file) == "c":
            code = buildC(file)
        elif getExtension(file) == "asm":
            code = buildASM(file)
        elif getExtension(file) == "cc":
            code = buildCXX(file)
        else:
            print(f"Invalid or unhandled extension {getExtension(file)}")
            exit(1)

        for incPath in CONFIG["INCPATHS"]:
            CONFIG["CFLAGS"].remove(incPath)
            CONFIG["ASFLAGS"].remove(incPath)

        if code != 0:
            callCmd(f"rm -f ./.build-cache/{basename}/cache/{file}")
            exit(code)

    buildAR(f"{CONFIG["outDir"][0]}/{directory}", out_file)

def buildDir(directory, static_lib: bool, out_file="a.out"):
    if static_lib:
        buildStaticLib(directory, out_file)
    else:
        buildKernel(directory)

def setupLimine():
    build_limine: bool = False
    if not os.path.exists("Limine"):
        build_limine = True
    elif not os.path.exists("Limine/bin/BOOTX64.EFI"):
        build_limine = True
    if build_limine:
        print("Building limine")
        callCmd("rm -rf Limine")
        callCmd("git clone https://github.com/Limine-Bootloader/Limine --depth=1", True)
        os.chdir("Limine")
        callCmd("./bootstrap")
        callCmd("./configure --enable-uefi-x86-64 --enable-bios")
        callCmd("make")
        os.chdir("..")
        callCmd("cp ./Limine/limine-protocol/include/limine.h ./Limine/limine.h")
        callCmd("rm -rf ./Limine/commands.txt")
    callCmd(f"cp ./util/limine.conf {CONFIG['outDir'][0]}/limine.conf")
    callCmd(f"cp ./Limine/bin/limine-bios.sys {CONFIG['outDir'][0]}/limine-bios.sys")
    callCmd(f"cp ./Limine/bin/BOOT* {CONFIG['outDir'][0]}/")

def getInfo():
    callCmd("rm -f info.txt")
    callCmd("touch info.txt")
    callCmd(f"cloc . --exclude-dir=Limine,bin,.build-cache,script,.vscode >> info.txt")
    callCmd(f"tree -I 'bin' -I 'Limine' -I 'script' -I '.vscode' -I 'tmp.txt' -I 'commands.txt' -I 'info.txt' >> info.txt")

def cleanFiles(dirs: list[str]):
    for dir_ in dirs:
        files = glob.glob(dir_+'/**', recursive=True)
        objFiles = glob.glob(CONFIG['outDir'][0]+'/'+dir_+'/**', recursive=True)
        newObjFiles = []
        for objFile in objFiles:
            if os.path.isfile(objFile) and checkExtension(objFile, ["o", ".bc"]):
                objFile.removesuffix(getExtension(objFile))
                newObjFiles.append(objFile)
        for file in files:
            if not os.path.isfile(file) or not checkExtension(file, ["c", "cc", "asm"]):
                continue
            file = CONFIG["outDir"][0] + '/' + file + '.o'
            if file not in newObjFiles:
                print(f"RM    {file}")
                callCmd(f"rm -f {file}")

def atExitFunc():
    currentUser = os.getlogin()
    callCmd(f"chown -R {currentUser}:{currentUser} ./")

def main():
    global force_rebuild
    basename = os.path.basename(os.path.dirname(os.path.realpath(__file__)))
    atexit.register(atExitFunc)
    if "clean" in sys.argv:
        callCmd(f"rm -rf ./.build-cache/{basename}")
        callCmd(f"rm -rf {CONFIG['outDir'][0]}")
        force_rebuild = True
    if "clean-all" in sys.argv:
        callCmd(f"rm -rf Limine")
        callCmd(f"rm -rf ./.build-cache/{basename}")
        callCmd(f"rm -rf {CONFIG['outDir'][0]}")
        force_rebuild = True
    if force_rebuild:
        print("Rebuilding...")
        callCmd(f"rm -rf ./.build-cache/{basename}")
        callCmd(f"rm -rf {CONFIG['outDir'][0]}")
    if "build-bootloader" in sys.argv:
        callCmd(f"rm -rf Limine")
        setupLimine()
    print("> Creating necesarry dirs")
    callCmd(f"mkdir -p {CONFIG['outDir'][0]}")
    callCmd(f"mkdir -p {CONFIG['outDir'][0]}/kernel")
    if 'limine-uefi' in CONFIG["bootloader"] and "build-bootloader" not in sys.argv:
        setupLimine()
    else:
        print("TODO: Other bootloaders")
        exit(1)
    if "compile" in sys.argv:
        callCmd(f"rm -rf {CONFIG["outDir"][0]}/init/libc/init/start.S.o")
        print("> Building Libc")
        if CONFIG["asan"][0] == "yes":
            CONFIG["CFLAGS"].remove("-fsanitize=kernel-address")
            CONFIG["CFLAGS"].append("-fsanitize=address")
        buildDir("libc", True, f"{CONFIG['outDir'][0]}/libc.a")
        if CONFIG["asan"][0] == "yes":
            CONFIG["CFLAGS"].remove("-fsanitize=address")
            CONFIG["CFLAGS"].append("-fsanitize=kernel-address")
        print("> Building drivers")
        buildDir("drivers", True, f"{CONFIG['outDir'][0]}/drivers.a")
        print("> Building common")
        buildDir("common", True, f"{CONFIG['outDir'][0]}/common.a")
        print("> Building kernel")
        buildDir("kernel", False)
        print("> Building init")
        CONFIG["CFLAGS"] += ['-nostdinc']
        CONFIG["INCPATHS"] = ['-Iinit/include', '-Iinit/include/libc']
        CONFIG["CFLAGS"].remove('-fno-pie')
        CONFIG["CFLAGS"].remove('-fno-PIE')
        CONFIG["CFLAGS"].remove('-fno-pic')
        CONFIG["CFLAGS"].remove('-fno-PIC')
        CONFIG["CFLAGS"].append('-fpie')
        CONFIG["CFLAGS"].append('-fPIE')
        CONFIG["CFLAGS"].append('-fpic')
        CONFIG["CFLAGS"].append('-fPIC')
        CONFIG["CFLAGS"].remove('-mcmodel=kernel')
        CONFIG["CFLAGS"].remove('-mno-tls-direct-seg-refs')
        CONFIG["CFLAGS"].remove('-mno-red-zone')
        CONFIG["CFLAGS"].remove('-fno-stack-protector')
        if CONFIG["asan"][0] == "yes":
            CONFIG["CFLAGS"].remove("-fsanitize=kernel-address")
            # CONFIG["CFLAGS"].append("-fsanitize=address")
        CONFIG["CXXFLAGS"].remove('-fno-exceptions')
        CONFIG["CXXFLAGS"].remove('-fno-rtti') 
        buildDir("init/src", False)
        print("> Building shell")
        buildDir("init/shell", False)
        print("> Building init's libc")
        buildDir("init/libc", True, f"{CONFIG['outDir'][0]}/libc_init.a")
        buildSO(f"{CONFIG['outDir'][0]}/init/libc", f"{CONFIG['outDir'][0]}/libc_init.so")
        print("> Building start.asm")
        callCmd(f"as -o {CONFIG['outDir'][0]}/init/libc/init/start.S.o ./init/libc/init/start.S")
        print("> Removing unused objects")
        cleanFiles(["libc", "drivers", "common", "kernel", "test"])
        print("> Linking kernel")
        linkDir(f"{CONFIG['outDir'][0]}/kernel", "util/linker.ld", "kernel", [f"{CONFIG['outDir'][0]}/libc.a", f"{CONFIG['outDir'][0]}/drivers.a", f"{CONFIG['outDir'][0]}/common.a"])
        CONFIG["LDFLAGS"].append('-Wl,--oformat=binary')
        linkDir(f"{CONFIG['outDir'][0]}/kernel", "util/linker.ld", "kernel.binary", [f"{CONFIG['outDir'][0]}/libc.a", f"{CONFIG['outDir'][0]}/drivers.a", f"{CONFIG['outDir'][0]}/common.a"])
        CONFIG["LDFLAGS"].remove('-Wl,--oformat=binary')
        print("> Linking init")
        CONFIG["LDFLAGS"].remove('-fno-pie')
        CONFIG["LDFLAGS"].remove('-fno-PIE')
        CONFIG["LDFLAGS"].remove('-fno-pic')
        CONFIG["LDFLAGS"].remove('-fno-PIC')
        CONFIG["LDFLAGS"].remove('-mcmodel=kernel')
        CONFIG["LDFLAGS"].remove('-Wl,-no-pie')
        CONFIG["LDFLAGS"].append(f'{CONFIG['outDir'][0]}/libc_init.a')
        CONFIG["LDFLAGS"].append(f'-static')
        if CONFIG["asan"][0] == "yes":
            CONFIG["LDFLAGS"].remove("-fsanitize=kernel-address")
            # CONFIG["LDFLAGS"].append("-fsanitize=address")
        CONFIG["LDFLAGS"].append(f"{CONFIG["outDir"][0]}/init/libc/init/start.S.o")
        # linkDir(f"{CONFIG['outDir'][0]}/init/src", None, "init", [f"{CONFIG['outDir'][0]}/libc_init.a"])
        linkDir(f"{CONFIG['outDir'][0]}/init/src", None, "init")
        print("> Linking shell")
        CONFIG["LDFLAGS"].remove(f'-static')
        CONFIG["LDFLAGS"].remove(f'{CONFIG['outDir'][0]}/libc_init.a')
        CONFIG["LDFLAGS"].append('-Lbin')
        CONFIG["LDFLAGS"].append('-l:libc_init.so')
        linkDir(f"{CONFIG['outDir'][0]}/init/shell", None, "shell")
        print("> Getting info")
        getInfo()
        buildImage(f"{CONFIG['outDir'][0]}/image.img", f"{CONFIG['outDir'][0]}/BOOT*", f"{CONFIG['outDir'][0]}/kernel.elf", f"{CONFIG['outDir'][0]}/init.img", [])
        # if os.path.exists("/dev/sda"):
        #     buildImage(f"/dev/sda", f"{CONFIG['outDir'][0]}/BOOT*", f"{CONFIG['outDir'][0]}/kernel.elf", [f"{CONFIG['outDir'][0]}/init.img"])
    if "run" in sys.argv:
        print("> Running QEMU")
        callCmd(f"./script/run.sh {CONFIG['outDir'][0]} {CONFIG['config'][0]}", True)

if __name__ == '__main__':
    main()