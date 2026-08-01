#!/usr/bin/env bash
#
# Build the bootable GooseBoot test ISOs from already-built firmware.
#
# The images are meant for a disposable virtual machine. This script only ever
# writes ordinary files inside the output directory it is given: it never opens
# a block device, never touches a partition table, and installs nothing.
#
#   gooseboot.iso       Boots on both firmware types. Its catalog follows the
#                       layout every mainstream installer ISO uses: a BIOS
#                       default entry, then an EFI entry behind a platform
#                       section header. Firmware that only scans section
#                       headers for platform 0xEF - VMware among them - does
#                       not see an EFI entry that was written as the default
#                       one, so the section header is what makes UEFI work.
#   gooseboot-bios.iso  BIOS only, for a machine that should never be offered
#                       an EFI path.
#
# The ESP is FAT16 rather than FAT12: both are allowed for removable media, and
# FAT16 is the better supported of the two across real firmware.
#
# Requires: xorriso (xorrisofs), mtools, dosfstools.

set -euo pipefail

firmware_dir=${1:-build-firmware/boot/firmware}
output_dir=${2:-build-firmware/boot/iso}

die() { printf 'make_boot_isos: %s\n' "$1" >&2; exit 1; }

case ${output_dir} in
    /dev/*|/sys/*|/proc/*) die "refusing to write to a device path: ${output_dir}" ;;
esac
[ -d "${firmware_dir}" ] || die "firmware directory not found: ${firmware_dir}"

efi=${firmware_dir}/GooseBootX64.efi
stage2=${firmware_dir}/gooseboot-bios-stage2.bin
cdstub=${firmware_dir}/gooseboot-bios-cdstub.bin
for required in "${efi}" "${stage2}" "${cdstub}"; do
    [ -f "${required}" ] || die "missing firmware artifact: ${required}"
done

size_of() { wc -c < "$1" | tr -d ' '; }
[ "$(size_of "${cdstub}")" = "512" ] || die "the CD stub must be exactly 512 bytes"
[ "$(size_of "${stage2}")" = "65024" ] || die "stage 2 must be exactly 127 sectors"

command -v mformat >/dev/null || die "mtools is required (mformat, mcopy)"
command -v mkfs.vfat >/dev/null || die "dosfstools is required (mkfs.vfat)"
if command -v xorrisofs >/dev/null; then
    iso_tool=xorrisofs
elif command -v xorriso >/dev/null; then
    iso_tool="xorriso -as mkisofs"
else
    die "xorriso is required (provides xorrisofs)"
fi

mkdir -p "${output_dir}"
staging=${output_dir}/staging
rm -rf "${staging}"
mkdir -p "${staging}/tree/EFI/BOOT" "${staging}/bios"

# --- The EFI side: a small FAT16 system partition carried inside the ISO -----
esp=${staging}/tree/esp.img
dd if=/dev/zero of="${esp}" bs=1024 count=4096 status=none
mkfs.vfat -F 16 -s 1 -n GOOSEESP "${esp}" >/dev/null
mmd -i "${esp}" ::/EFI ::/EFI/BOOT
mcopy -i "${esp}" "${efi}" ::/EFI/BOOT/BOOTX64.EFI
# Also expose the application in the ISO9660 tree for firmware that browses it.
cp "${efi}" "${staging}/tree/EFI/BOOT/BOOTX64.EFI"

# --- The BIOS side: stub plus stage 2, one 128-sector no-emulation image -----
cat "${cdstub}" "${stage2}" > "${staging}/tree/gooseboot-cd.bin"
[ "$(size_of "${staging}/tree/gooseboot-cd.bin")" = "65536" ] ||
    die "the CD boot image must be exactly 128 sectors"

${iso_tool} -quiet -r -J -V GOOSEBOOT67 -c boot.catalog \
    -b gooseboot-cd.bin -no-emul-boot -boot-load-size 128 \
    -eltorito-alt-boot -e esp.img -no-emul-boot \
    -o "${output_dir}/gooseboot.iso" "${staging}/tree"

cp "${staging}/tree/gooseboot-cd.bin" "${staging}/bios/"
${iso_tool} -quiet -r -J -V GOOSEBOOT67 -c boot.catalog \
    -b gooseboot-cd.bin -no-emul-boot -boot-load-size 128 \
    -o "${output_dir}/gooseboot-bios.iso" "${staging}/bios"

rm -rf "${staging}"

printf '\nWrote:\n'
for image in "${output_dir}/gooseboot.iso" "${output_dir}/gooseboot-bios.iso"; do
    printf '  %s  %s bytes\n' "${image}" "$(size_of "${image}")"
done
if command -v sha256sum >/dev/null; then
    printf '\n'
    sha256sum "${output_dir}"/gooseboot-*.iso
fi
printf '\nBoot these in a throwaway VM only. They are unsigned and install nothing.\n'
