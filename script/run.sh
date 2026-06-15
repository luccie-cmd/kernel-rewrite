qemu-system-x86_64 \
    -m 2G \
    -debugcon file:debug.log \
    -global isa-debugcon.iobase=0xe9 \
    -no-reboot \
    -d int,cpu_reset,in_asm,fpu \
    -D qemu.log \
    -M q35 \
    -smp 1 \
    -drive file="./bin/image.img",if=none,format=raw,id=nvme0 \
    -device nvme,drive=nvme0,serial=QEMUNVME123456789,id=nvme0_dev \
    -device e1000,netdev=net0 \
    -netdev user,id=net0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -bios /usr/share/OVMF/x64/OVMF.4m.fd \
    -cpu max,-x2apic \
    # -enable-kvm \
    # -cpu host
    # -drive id=disk,file="/dev/sda",format=raw,if=ide