Import("env")

from os.path import join

ed25519 = join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "MeshCore", "lib", "ed25519")
env.Append(CPPPATH=[ed25519])
env.BuildSources("$BUILD_DIR/ed25519", ed25519)


def exclude_unused_soil_sensor(build_env, node):
    return None


env.AddBuildMiddleware(exclude_unused_soil_sensor, "*/RAK12035_SoilMoisture.cpp")
