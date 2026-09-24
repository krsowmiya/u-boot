#!/usr/bin/env python3
"""
Standalone script to pack xbl_sec.mbn as a segment into xbl.elf

This script extracts the xbl_sec embedding functionality from createxbl.py
and provides a simple interface to pack xbl_sec.mbn into an existing xbl.elf file.

Usage:
    python pack_xbl_sec_standalone.py -x <xbl_sec.mbn> -e <xbl.elf> -o <output.elf> -a <32|64>

Example:
    python pack_xbl_sec_standalone.py -x xbl_sec.mbn -e xbl.elf -o xbl_final.elf -a 64
"""

from __future__ import print_function
import os
import sys
import struct
import shutil
from optparse import OptionParser

# Constants
PAGE_SIZE = 4096
SEGMENT_ALIGN_4K = 4096
ELF32_HDR_SIZE = 52
ELF32_PHDR_SIZE = 32
ELF64_HDR_SIZE = 64
ELF64_PHDR_SIZE = 56

# ELF Magic numbers
ELFINFO_MAG0 = b'\x7f'
ELFINFO_MAG1 = b'E'
ELFINFO_MAG2 = b'L'
ELFINFO_MAG3 = b'F'
ELFINFO_CLASS_INDEX = 4
ELFINFO_CLASS_32 = b'\x01'
ELFINFO_CLASS_64 = b'\x02'

# Segment type flags
MI_PBT_FLAG_SEGMENT_TYPE_SHIFT = 0x18
MI_PBT_SWAPPED_SEGMENT = 0x5


def convert_bytes_to_int(bytes_val):
    """Convert bytes to integer"""
    if sys.version_info > (3, 0, 0):
        return int.from_bytes(bytes_val, byteorder='little')
    else:
        return bytes_val


def roundup(x, precision):
    """Round up to nearest precision"""
    return x if x % precision == 0 else (x + precision - (x % precision))


class Elf32_Ehdr:
    """ELF32 Header Class"""
    s = struct.Struct('16sHHIIIIIHHHHHH')

    def __init__(self, data):
        unpacked_data = (Elf32_Ehdr.s).unpack(data)
        self.e_ident = unpacked_data[0]
        self.e_type = unpacked_data[1]
        self.e_machine = unpacked_data[2]
        self.e_version = unpacked_data[3]
        self.e_entry = unpacked_data[4]
        self.e_phoff = unpacked_data[5]
        self.e_shoff = unpacked_data[6]
        self.e_flags = unpacked_data[7]
        self.e_ehsize = unpacked_data[8]
        self.e_phentsize = unpacked_data[9]
        self.e_phnum = unpacked_data[10]
        self.e_shentsize = unpacked_data[11]
        self.e_shnum = unpacked_data[12]
        self.e_shstrndx = unpacked_data[13]

    def getPackedData(self):
        values = [self.e_ident, self.e_type, self.e_machine, self.e_version,
                  self.e_entry, self.e_phoff, self.e_shoff, self.e_flags,
                  self.e_ehsize, self.e_phentsize, self.e_phnum,
                  self.e_shentsize, self.e_shnum, self.e_shstrndx]
        return (Elf32_Ehdr.s).pack(*values)


class Elf32_Phdr:
    """ELF32 Program Header Class"""
    s = struct.Struct('I' * 8)

    def __init__(self, data):
        unpacked_data = (Elf32_Phdr.s).unpack(data)
        self.p_type = unpacked_data[0]
        self.p_offset = unpacked_data[1]
        self.p_vaddr = unpacked_data[2]
        self.p_paddr = unpacked_data[3]
        self.p_filesz = unpacked_data[4]
        self.p_memsz = unpacked_data[5]
        self.p_flags = unpacked_data[6]
        self.p_align = unpacked_data[7]

    def getPackedData(self):
        values = [self.p_type, self.p_offset, self.p_vaddr, self.p_paddr,
                  self.p_filesz, self.p_memsz, self.p_flags, self.p_align]
        return (Elf32_Phdr.s).pack(*values)


class Elf64_Ehdr:
    """ELF64 Header Class"""
    s = struct.Struct('16sHHIQQQIHHHHHH')

    def __init__(self, data):
        unpacked_data = (Elf64_Ehdr.s).unpack(data)
        self.e_ident = unpacked_data[0]
        self.e_type = unpacked_data[1]
        self.e_machine = unpacked_data[2]
        self.e_version = unpacked_data[3]
        self.e_entry = unpacked_data[4]
        self.e_phoff = unpacked_data[5]
        self.e_shoff = unpacked_data[6]
        self.e_flags = unpacked_data[7]
        self.e_ehsize = unpacked_data[8]
        self.e_phentsize = unpacked_data[9]
        self.e_phnum = unpacked_data[10]
        self.e_shentsize = unpacked_data[11]
        self.e_shnum = unpacked_data[12]
        self.e_shstrndx = unpacked_data[13]

    def getPackedData(self):
        values = [self.e_ident, self.e_type, self.e_machine, self.e_version,
                  self.e_entry, self.e_phoff, self.e_shoff, self.e_flags,
                  self.e_ehsize, self.e_phentsize, self.e_phnum,
                  self.e_shentsize, self.e_shnum, self.e_shstrndx]
        return (Elf64_Ehdr.s).pack(*values)


class Elf64_Phdr:
    """ELF64 Program Header Class"""
    s = struct.Struct('IIQQQQQQ')

    def __init__(self, data):
        unpacked_data = (Elf64_Phdr.s).unpack(data)
        self.p_type = unpacked_data[0]
        self.p_flags = unpacked_data[1]
        self.p_offset = unpacked_data[2]
        self.p_vaddr = unpacked_data[3]
        self.p_paddr = unpacked_data[4]
        self.p_filesz = unpacked_data[5]
        self.p_memsz = unpacked_data[6]
        self.p_align = unpacked_data[7]

    def getPackedData(self):
        values = [self.p_type, self.p_flags, self.p_offset, self.p_vaddr,
                  self.p_paddr, self.p_filesz, self.p_memsz, self.p_align]
        return (Elf64_Phdr.s).pack(*values)


def preprocess_elf_file(elf_file_name):
    """Read and parse ELF file headers"""
    with open(elf_file_name, 'rb') as elf_fp:
        # Read first 5 bytes to determine ELF class
        elf_fp.seek(0)
        ident = elf_fp.read(5)
        
        # Verify ELF magic
        if ident[0:4] != ELFINFO_MAG0 + ELFINFO_MAG1 + ELFINFO_MAG2 + ELFINFO_MAG3:
            raise RuntimeError("Not a valid ELF file: " + elf_file_name)
        
        is_64bit = (ident[ELFINFO_CLASS_INDEX:ELFINFO_CLASS_INDEX+1] == ELFINFO_CLASS_64)
        
        # Read full header
        elf_fp.seek(0)
        if is_64bit:
            elf_header = Elf64_Ehdr(elf_fp.read(ELF64_HDR_SIZE))
            phdr_size = ELF64_PHDR_SIZE
        else:
            elf_header = Elf32_Ehdr(elf_fp.read(ELF32_HDR_SIZE))
            phdr_size = ELF32_PHDR_SIZE
        
        # Read program headers
        phdr_table = []
        elf_fp.seek(elf_header.e_phoff)
        for i in range(elf_header.e_phnum):
            if is_64bit:
                phdr_table.append(Elf64_Phdr(elf_fp.read(phdr_size)))
            else:
                phdr_table.append(Elf32_Phdr(elf_fp.read(phdr_size)))
    
    return [elf_header, phdr_table, is_64bit]


def file_copy_offset(in_fp, in_offset, out_fp, out_offset, num_bytes):
    """Copy data from input file to output file at specified offsets"""
    in_fp.seek(in_offset)
    data = in_fp.read(num_bytes)
    out_fp.seek(out_offset)
    out_fp.write(data)
    return num_bytes


def pack_xbl_sec(xbl_elf_path, xbl_sec_path, output_path, is_out_elf_64_bit):
    """
    Pack xbl_sec.mbn as a segment into xbl.elf
    
    Args:
        xbl_elf_path: Path to input xbl.elf file
        xbl_sec_path: Path to xbl_sec.mbn file
        output_path: Path to output ELF file
        is_out_elf_64_bit: True for 64-bit ELF, False for 32-bit
    """
    
    print("Processing XBL ELF file: " + xbl_elf_path)
    print("Processing XBL_SEC file: " + xbl_sec_path)
    print("Output file: " + output_path)
    
    # Parse input ELF files
    [elf_header_xbl, phdr_table_xbl, is_xbl_64bit] = preprocess_elf_file(xbl_elf_path)
    [elf_header_xblsec, phdr_table_xblsec, is_xblsec_64bit] = preprocess_elf_file(xbl_sec_path)
    
    # Use the input file's architecture, not the parameter
    # The output must match the input XBL ELF architecture
    is_out_elf_64_bit = is_xbl_64bit
    
    print("Input XBL is " + ("64-bit" if is_xbl_64bit else "32-bit"))
    print("Input XBL_SEC is " + ("64-bit" if is_xblsec_64bit else "32-bit"))
    print("Output will be " + ("64-bit" if is_out_elf_64_bit else "32-bit"))
    
    # Open files
    xbl_fp = open(xbl_elf_path, 'rb')
    xblsec_fp = open(xbl_sec_path, 'rb')
    out_fp = open(output_path, 'wb+')
    
    # Get input file size to know where to append xbl_sec
    xbl_input_size = os.path.getsize(xbl_elf_path)
    
    # Calculate program header table size
    if is_out_elf_64_bit:
        phdr_size = ELF64_PHDR_SIZE
        elf_hdr_size = ELF64_HDR_SIZE
    else:
        phdr_size = ELF32_PHDR_SIZE
        elf_hdr_size = ELF32_HDR_SIZE
    
    # Calculate new program header count (existing + 1 for xbl_sec)
    new_phdr_count = elf_header_xbl.e_phnum + 1
    
    # Update ELF header with new program header count
    elf_header_xbl.e_phnum = new_phdr_count
    
    # Write updated ELF header
    out_fp.seek(0)
    out_fp.write(elf_header_xbl.getPackedData())
    
    # Copy existing program headers
    print("Copying existing program headers...")
    xbl_fp.seek(elf_header_xbl.e_phoff)
    existing_phdrs_data = xbl_fp.read(elf_header_xbl.e_phentsize * (new_phdr_count - 1))
    out_fp.seek(elf_header_xbl.e_phoff)
    out_fp.write(existing_phdrs_data)
    
    # Copy all segment data from input file (everything after headers)
    print("Copying existing XBL segments...")
    # Find the start of segment data (after program headers)
    seg_data_start = elf_header_xbl.e_phoff + (elf_header_xbl.e_phentsize * (new_phdr_count - 1))
    xbl_fp.seek(seg_data_start)
    remaining_data = xbl_fp.read()
    out_fp.seek(seg_data_start)
    out_fp.write(remaining_data)
    
    # Now add xbl_sec as a new segment
    print("Adding XBL_SEC as new segment...")
    
    # Find entry point segment in xbl_sec to calculate physical/virtual address
    entry_seg_offset = -1
    entry_addr = elf_header_xblsec.e_entry
    
    for i in range(elf_header_xblsec.e_phnum):
        phdr = phdr_table_xblsec[i]
        max_addr = phdr.p_vaddr + phdr.p_memsz - 1
        if phdr.p_vaddr <= entry_addr <= max_addr:
            entry_seg_offset = phdr.p_offset
            break
    
    if entry_seg_offset == -1:
        raise RuntimeError("Failed to find entry point in xbl_sec segment!")
    
    # Calculate physical/virtual address for xbl_sec segment
    phys_virt_addr = entry_addr - entry_seg_offset
    
    # Get xbl_sec file size
    xbl_sec_size = os.path.getsize(xbl_sec_path)
    
    # Calculate where to place xbl_sec segment (at end of file, aligned)
    segment_offset = xbl_input_size
    if segment_offset % SEGMENT_ALIGN_4K != 0:
        segment_offset = roundup(segment_offset, SEGMENT_ALIGN_4K)
    
    # Create program header for xbl_sec
    # Calculate flags: R+E (0x5) with swapped segment type in upper bits
    phdr_flags = 0x5 | (MI_PBT_SWAPPED_SEGMENT << MI_PBT_FLAG_SEGMENT_TYPE_SHIFT)
    
    if is_out_elf_64_bit:
        xblsec_phdr = Elf64_Phdr(b'\0' * ELF64_PHDR_SIZE)
        xblsec_phdr.p_type = 0x1  # PT_LOAD
        xblsec_phdr.p_flags = phdr_flags
        xblsec_phdr.p_offset = segment_offset
        xblsec_phdr.p_vaddr = phys_virt_addr
        xblsec_phdr.p_paddr = phys_virt_addr
        xblsec_phdr.p_filesz = xbl_sec_size
        xblsec_phdr.p_memsz = xbl_sec_size
        xblsec_phdr.p_align = 0x1000
    else:
        xblsec_phdr = Elf32_Phdr(b'\0' * ELF32_PHDR_SIZE)
        xblsec_phdr.p_type = 0x1  # PT_LOAD
        xblsec_phdr.p_offset = segment_offset
        xblsec_phdr.p_vaddr = phys_virt_addr
        xblsec_phdr.p_paddr = phys_virt_addr
        xblsec_phdr.p_filesz = xbl_sec_size
        xblsec_phdr.p_memsz = xbl_sec_size
        xblsec_phdr.p_flags = phdr_flags
        xblsec_phdr.p_align = 0x1000
    
    # Write xbl_sec program header at the end of program header table
    phdr_offset = elf_header_xbl.e_phoff + (elf_header_xbl.e_phentsize * (new_phdr_count - 1))
    out_fp.seek(phdr_offset)
    out_fp.write(xblsec_phdr.getPackedData())
    
    # Pad to alignment if needed before writing xbl_sec data
    current_size = xbl_input_size
    if segment_offset > current_size:
        out_fp.seek(current_size)
        padding_needed = segment_offset - current_size
        out_fp.write(b'\0' * padding_needed)
    
    # Copy entire xbl_sec file as segment data
    out_fp.seek(segment_offset)
    xblsec_fp.seek(0)
    xblsec_data = xblsec_fp.read(xbl_sec_size)
    out_fp.write(xblsec_data)
    
    # Close files
    xbl_fp.close()
    xblsec_fp.close()
    out_fp.close()
    
    print("Successfully packed xbl_sec.mbn into " + output_path)
    print("XBL_SEC segment added at offset: 0x{:X}".format(xblsec_phdr.p_offset))
    print("XBL_SEC physical address: 0x{:X}".format(xblsec_phdr.p_paddr))


def main():
    parser = OptionParser(usage='usage: %prog -x <xbl_sec.mbn> -e <xbl.elf> -o <output.elf> -a <32|64>')
    
    parser.add_option("-x", "--xbl_sec",
                      action="store", type="string", dest="xbl_sec_path",
                      help="Path to xbl_sec.mbn file")
    
    parser.add_option("-e", "--xbl_elf",
                      action="store", type="string", dest="xbl_elf_path",
                      help="Path to input xbl.elf file")
    
    parser.add_option("-o", "--output",
                      action="store", type="string", dest="output_path",
                      help="Path to output ELF file")
    
    parser.add_option("-a", "--arch",
                      action="store", type="string", dest="arch",
                      help="Output ELF architecture: '32' or '64'")
    
    (options, args) = parser.parse_args()
    
    # Validate arguments
    if not options.xbl_sec_path:
        parser.error('xbl_sec.mbn file path not provided')
    
    if not options.xbl_elf_path:
        parser.error('xbl.elf file path not provided')
    
    if not options.output_path:
        parser.error('Output file path not provided')
    
    if not options.arch:
        parser.error('Architecture not specified (32 or 64)')
    
    if options.arch not in ['32', '64']:
        parser.error('Architecture must be either 32 or 64')
    
    # Check if input files exist
    if not os.path.exists(options.xbl_sec_path):
        parser.error('xbl_sec.mbn file does not exist: ' + options.xbl_sec_path)
    
    if not os.path.exists(options.xbl_elf_path):
        parser.error('xbl.elf file does not exist: ' + options.xbl_elf_path)
    
    # Determine output architecture
    is_64bit = (options.arch == '64')
    
    # Pack xbl_sec into xbl.elf
    try:
        pack_xbl_sec(options.xbl_elf_path, options.xbl_sec_path, 
                    options.output_path, is_64bit)
    except Exception as e:
        print("ERROR: " + str(e))
        sys.exit(1)


if __name__ == "__main__":
    main()
