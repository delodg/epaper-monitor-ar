# Script previo de PlatformIO: evita que el binario embeba rutas absolutas de esta PC
# (__FILE__ en asserts/logs revela el usuario de Windows). Reemplaza los prefijos de la
# carpeta de paquetes y del proyecto por nombres neutros.
Import("env")

pk = env.subst("$PROJECT_PACKAGES_DIR").replace("\\", "/")
pd = env.subst("$PROJECT_DIR").replace("\\", "/")
flags = [f"-fmacro-prefix-map={pk}=pkg", f"-fmacro-prefix-map={pd}=.",
         f"-ffile-prefix-map={pk}=pkg", f"-ffile-prefix-map={pd}=."]
env.Append(CCFLAGS=flags, ASFLAGS=flags)
