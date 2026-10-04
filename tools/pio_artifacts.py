"""PlatformIO: imagen completa por entorno, con offsets del propio builder."""
import json
from pathlib import Path
import subprocess

Import("env")


def merge_firmware(source, target, env):
    build = Path(env.subst("$BUILD_DIR"))
    merged = build / "firmware-merged.bin"
    images = [(str(offset), env.subst(str(path)))
              for offset, path in env.get("FLASH_EXTRA_IMAGES", [])]
    images.append((env.subst("$ESP32_APP_OFFSET"), str(build / "firmware.bin")))
    chip = env.BoardConfig().get("build.mcu")
    flash_size = env.BoardConfig().get("upload.flash_size")
    command = [env.subst("$PYTHONEXE"), env.subst("$UPLOADER"), "--chip", chip,
               "merge_bin", "-o", str(merged), "--flash_mode",
               env.subst("${__get_board_flash_mode(__env__)}"), "--flash_freq",
               env.subst("${__get_board_f_image(__env__)}"), "--flash_size", flash_size]
    for offset, path in images:
        command.extend([offset, path])
    subprocess.run(command, check=True)
    version = next(str(item[1]).strip('\\"') for item in env.get("CPPDEFINES", [])
                   if isinstance(item, tuple) and item[0] == "FW_VERSION")
    (build / "artifact.json").write_text(json.dumps({
        "environment": env.subst("$PIOENV"), "version": version,
        "chip": chip, "flashSize": flash_size, "offset": 0,
    }, indent=2) + "\n", encoding="utf-8")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)
