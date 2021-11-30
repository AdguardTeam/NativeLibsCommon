for /d %%f in (../conan/recipes/*) do conan export %%f AdguardTeam/NativeLibsCommon
conan export ../common AdguardTeam/NativeLibsCommon