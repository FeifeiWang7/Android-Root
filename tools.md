#### objdump

objdump for arm locates in the ndk toolchain

	android-ndk-r10e/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-objdump 
Usage

* -b is followed by the file type binary
* -marm specifies the cpu is ARM.

To disassemble a kernel image

	android-ndk-r10e/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-objdump -D -b binary -marm kernel.Image  > a
