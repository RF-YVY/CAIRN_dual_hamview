Import("env")
env.Append(LINKFLAGS=["-Wl,--wrap=esp_ota_get_next_update_partition"])


def select_resident_image(target, source, env):
    flags = list(env["UPLOADERFLAGS"])
    end = flags.index("--flash-size") + 2
    env.Replace(ESP32_APP_OFFSET="0x3f0000", UPLOADERFLAGS=flags[:end])


env.AddPreAction("upload", select_resident_image)
