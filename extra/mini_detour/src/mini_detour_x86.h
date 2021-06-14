#ifndef MINI_DETOUR_X86_H
#define MINI_DETOUR_X86_H

// http://ref.x86asm.net/coder32.html

struct opcode_t
{
    bool has_r_m;
    uint8_t base_size;
    const char* desc;
};
opcode_t s_1byte_opcodes[256] =
{
    {false, 0, "0x00"}, // 0x00
    {false, 0, "0x01"}, // 0x01
    {false, 0, "0x02"}, // 0x02
    {false, 0, "0x03"}, // 0x03
    {false, 0, "0x04"}, // 0x04
    {false, 0, "0x05"}, // 0x05
    {false, 0, "0x06"}, // 0x06
    {false, 0, "0x07"}, // 0x07
    {false, 0, "0x08"}, // 0x08
    {false, 0, "0x09"}, // 0x09
    {false, 0, "0x0a"}, // 0x0a
    {false, 0, "0x0b"}, // 0x0b
    {false, 0, "0x0c"}, // 0x0c
    {false, 0, "0x0d"}, // 0x0d
    {false, 0, "0x0e"}, // 0x0e
    {false, 1, "Two-byte Instructions"}       , // 0x0f
    {true , 2, "ADC     r/m8        r8"}      , // 0x10
    {false, 0, "0x11"}, // 0x11
    {false, 0, "0x12"}, // 0x12
    {false, 0, "0x13"}, // 0x13
    {false, 0, "0x14"}, // 0x14
    {false, 0, "0x15"}, // 0x15
    {false, 0, "0x16"}, // 0x16
    {false, 0, "0x17"}, // 0x17
    {false, 0, "0x18"}, // 0x18
    {false, 0, "0x19"}, // 0x19
    {false, 0, "0x1a"}, // 0x1a
    {false, 0, "0x1b"}, // 0x1b
    {false, 0, "0x1c"}, // 0x1c
    {false, 0, "0x1d"}, // 0x1d
    {false, 0, "0x1e"}, // 0x1e
    {false, 0, "0x1f"}, // 0x1f
    {false, 0, "0x20"}, // 0x20
    {false, 0, "0x21"}, // 0x21
    {false, 0, "0x22"}, // 0x22
    {false, 0, "0x23"}, // 0x23
    {false, 0, "0x24"}, // 0x24
    {false, 5, "AND   eAX   imm16/32"}    , // 0x25
    {false, 0, "0x26"}, // 0x26
    {false, 0, "0x27"}, // 0x27
    {false, 0, "0x28"}, // 0x28
    {false, 0, "0x29"}, // 0x29
    {false, 0, "0x2a"}, // 0x2a
    {false, 0, "0x2b"}, // 0x2b
    {false, 0, "0x2c"}, // 0x2c
    {false, 0, "0x2d"}, // 0x2d
    {false, 0, "0x2e"}, // 0x2e
    {false, 0, "0x2f"}, // 0x2f
    {true , 2, "XOR     r/m8        r8"}      , // 0x30 
    {true , 2, "XOR     r/m16/32    r16/32"}  , // 0x31 
    {true , 2, "XOR     r8          r/m8"}    , // 0x32 
    {true , 2, "XOR     r16/32      r/m16/32"}, // 0x33 
    {false, 2, "XOR     AL          imm8"}    , // 0x34 
    {false, 5, "XOR     eAX         imm16/32"}, // 0x35
    {false, 0, "0x36"}, // 0x36
    {false, 0, "0x37"}, // 0x37
    {false, 0, "0x38"}, // 0x38
    {false, 0, "0x39"}, // 0x39
    {false, 0, "0x3a"}, // 0x3a
    {false, 0, "0x3b"}, // 0x3b
    {false, 0, "0x3c"}, // 0x3c
    {false, 0, "0x3d"}, // 0x3d
    {false, 0, "0x3e"}, // 0x3e
    {false, 0, "0x3f"}, // 0x3f
    {false, 1, "INC     eax"}                 , // 0x40 
    {false, 1, "INC     ecx"}                 , // 0x41 
    {false, 1, "INC     edx"}                 , // 0x42 
    {false, 1, "INC     ebx"}                 , // 0x43 
    {false, 1, "INC     esp"}                 , // 0x44 
    {false, 1, "INC     ebp"}                 , // 0x45 
    {false, 1, "INC     esi"}                 , // 0x46 
    {false, 1, "INC     edi"}                 , // 0x47 
    {false, 1, "DEC     eax"}                 , // 0x48
    {false, 1, "DEC     ecx"}                 , // 0x49
    {false, 1, "DEC     edx"}                 , // 0x4a
    {false, 1, "DEC     ebx"}                 , // 0x4b
    {false, 1, "DEC     esp"}                 , // 0x4c
    {false, 1, "DEC     ebp"}                 , // 0x4d
    {false, 1, "DEC     esi"}                 , // 0x4e
    {false, 1, "DEC     edi"}                 , // 0x4f
    {false, 1, "PUSH    eax"}                 , // 0x50
    {false, 1, "PUSH    ecx"}                 , // 0x51
    {false, 1, "PUSH    edx"}                 , // 0x52
    {false, 1, "PUSH    ebx"}                 , // 0x53
    {false, 1, "PUSH    esp"}                 , // 0x54
    {false, 1, "PUSH    ebp"}                 , // 0x55
    {false, 1, "PUSH    esi"}                 , // 0x56
    {false, 1, "PUSH    edi"}                 , // 0x57
    {false, 1, "POP     eax"}                 , // 0x58
    {false, 1, "POP     ecx"}                 , // 0x59
    {false, 1, "POP     edx"}                 , // 0x5a
    {false, 1, "POP     ebx"}                 , // 0x5b
    {false, 1, "POP     esp"}                 , // 0x5c
    {false, 1, "POP     ebp"}                 , // 0x5d
    {false, 1, "POP     esi"}                 , // 0x5e
    {false, 1, "POP     edi"}                 , // 0x5f
    {false, 0, "0x60"}, // 0x60
    {false, 0, "0x61"}, // 0x61
    {false, 0, "0x62"}, // 0x62
    {false, 0, "0x63"}, // 0x63
    {false, 1, "FS"}                          , // 0x64
    {false, 1, "GS"}                          , // 0x65
    {false, 0, "0x66"}, // 0x66
    {false, 0, "0x67"}, // 0x67
    {false, 5, "PUSH    imm16/32"}            , // 0x68
    {false, 0, "0x69"}, // 0x69
    {false, 2, "PUSH    imm8"}                , // 0x6a
    {false, 0, "0x6b"}, // 0x6b
    {false, 0, "0x6c"}, // 0x6c
    {false, 0, "0x6d"}, // 0x6d
    {false, 0, "0x6e"}, // 0x6e
    {false, 0, "0x6f"}, // 0x6f
    {false, 0, "0x70"}, // 0x70
    {false, 0, "0x71"}, // 0x71
    {false, 0, "0x72"}, // 0x72
    {false, 0, "0x73"}, // 0x73
    {false, 0, "0x74"}, // 0x74
    {false, 0, "0x75"}, // 0x75
    {false, 0, "0x76"}, // 0x76
    {false, 0, "0x77"}, // 0x77
    {false, 0, "0x78"}, // 0x78
    {false, 0, "0x79"}, // 0x79
    {false, 0, "0x7a"}, // 0x7a
    {false, 0, "0x7b"}, // 0x7b
    {false, 0, "0x7c"}, // 0x7c
    {false, 0, "0x7d"}, // 0x7d
    {false, 0, "0x7e"}, // 0x7e
    {false, 0, "0x7f"}, // 0x7f
    {false, 3, "ADD     r/m8        imm8"}    , // 0x80 
    {false, 6, "ADD     r/m16/32    imm16/32"}, // 0x81 
    {false, 3, "ADD     r/m8        imm8"}    , // 0x82 
    {false, 3, "ADD     r/m16/32    imm8"}    , // 0x83 
    {true , 2, "TEST    r/m8        r8"}      , // 0x84 
    {true , 2, "TEST    r/m16/32    r16/32"}  , // 0x85 
    {true , 2, "XCHG    r8          r/m8"}    , // 0x86 
    {true , 2, "XCHG    r16/32      r/m16/32"}, // 0x87 
    {true , 2, "MOV     r/m8        r8"}      , // 0x88 
    {true , 2, "MOV     r/m16/32    r16/32"}  , // 0x89 
    {true , 2, "MOV     r8          r/m8"}    , // 0x8a 
    {true , 2, "MOV     r16/32      r/m16/32"}, // 0x8b
    {true , 2, "MOV     m16         Sreg"}    , // 0x8c
    {true , 2, "LEA     r16/32      m"}       , // 0x8d
    {true , 2, "MOV     Sreg        r/m16"}   , // 0x8e
    {false, 2, "POP     r/m16/32"}            , // 0x8f
    {false, 1, "NOP"}                         , // 0x90
    {false, 0, "0x91"}, // 0x91
    {false, 0, "0x92"}, // 0x92
    {false, 0, "0x93"}, // 0x93
    {false, 0, "0x94"}, // 0x94
    {false, 0, "0x95"}, // 0x95
    {false, 0, "0x96"}, // 0x96
    {false, 0, "0x97"}, // 0x97
    {false, 0, "0x98"}, // 0x98
    {false, 0, "0x99"}, // 0x99
    {false, 0, "0x9a"}, // 0x9a
    {false, 0, "0x9b"}, // 0x9b
    {false, 0, "0x9c"}, // 0x9c
    {false, 0, "0x9d"}, // 0x9d
    {false, 0, "0x9e"}, // 0x9e
    {false, 0, "0x9f"}, // 0x9f
    {false, 0, "0xa0"}, // 0xa0
    {false, 5, "MOV   eAX   moffs16/32"}, // 0xa1
    {false, 0, "0xa2"}, // 0xa2
    {false, 0, "0xa3"}, // 0xa3
    {false, 0, "0xa4"}, // 0xa4
    {false, 0, "0xa5"}, // 0xa5
    {false, 0, "0xa6"}, // 0xa6
    {false, 0, "0xa7"}, // 0xa7
    {false, 0, "0xa8"}, // 0xa8
    {false, 0, "0xa9"}, // 0xa9
    {false, 0, "0xaa"}, // 0xaa
    {false, 0, "0xab"}, // 0xab
    {false, 0, "0xac"}, // 0xac
    {false, 0, "0xad"}, // 0xad
    {false, 0, "0xae"}, // 0xae
    {false, 0, "0xaf"}, // 0xaf
    {false, 2, "MOV     al          imm8"}    , // 0xb0
    {false, 2, "MOV     cl          imm8"}    , // 0xb1
    {false, 2, "MOV     dl          imm8"}    , // 0xb2
    {false, 2, "MOV     bl          imm8"}    , // 0xb3
    {false, 2, "MOV     ah          imm8"}    , // 0xb4
    {false, 2, "MOV     ch          imm8"}    , // 0xb5
    {false, 2, "MOV     dh          imm8"}    , // 0xb6
    {false, 2, "MOV     bh          imm8"}    , // 0xb7
    {false, 5, "MOV     eax         imm32"}   , // 0xb8
    {false, 5, "MOV     ecx         imm32"}   , // 0xb9
    {false, 5, "MOV     edx         imm32"}   , // 0xba
    {false, 5, "MOV     ebx         imm32"}   , // 0xbb
    {false, 5, "MOV     esp         imm32"}   , // 0xbc
    {false, 5, "MOV     ebp         imm32"}   , // 0xbd
    {false, 5, "MOV     esi         imm32"}   , // 0xbe
    {false, 5, "MOV     edi         imm32"}   , // 0xbf
    {false, 0, "0xc0"}, // 0xc0
    {false, 0, "0xc1"}, // 0xc1
    {false, 3, "RETN    imm16"}               , // 0xc2
    {false, 1, "RETN"}                        , // 0xc3
    {false, 0, "0xc4"}, // 0xc4
    {false, 0, "0xc5"}, // 0xc5
    {false, 0, "0xc6"}, // 0xc6
    {false, 0, "0xc7"}, // 0xc7
    {false, 0, "0xc8"}, // 0xc8
    {false, 1, "LEAVE   eBP"}                 , // 0xc9
    {false, 3, "RETF    imm16"}               , // 0xca
    {false, 1, "RETF"}                        , // 0xcb
    {false, 1, "INT     3"}                   , // 0xcc
    {false, 2, "INT     imm8"}                , // 0xcd
    {false, 1, "INTO    eFlags"}              , // 0xce
    {false, 1, "IRET    Flags"}               , // 0xcf
    {false, 0, "0xd0"}, // 0xd0
    {false, 0, "0xd1"}, // 0xd1
    {false, 0, "0xd2"}, // 0xd2
    {false, 0, "0xd3"}, // 0xd3
    {false, 0, "0xd4"}, // 0xd4
    {false, 0, "0xd5"}, // 0xd5
    {false, 0, "0xd6"}, // 0xd6
    {false, 0, "0xd7"}, // 0xd7
    {false, 0, "0xd8"}, // 0xd8
    {false, 0, "0xd9"}, // 0xd9
    {false, 0, "0xda"}, // 0xda
    {false, 0, "0xdb"}, // 0xdb
    {false, 0, "0xdc"}, // 0xdc
    {false, 0, "0xdd"}, // 0xdd
    {false, 0, "0xde"}, // 0xde
    {false, 0, "0xdf"}, // 0xdf
    {false, 0, "0xe0"}, // 0xe0
    {false, 0, "0xe1"}, // 0xe1
    {false, 0, "0xe2"}, // 0xe2
    {false, 0, "0xe3"}, // 0xe3
    {false, 0, "0xe4"}, // 0xe4
    {false, 0, "0xe5"}, // 0xe5
    {false, 0, "0xe6"}, // 0xe6
    {false, 0, "0xe7"}, // 0xe7
    {false, 5, "CALL    rel16/32"}            , // 0xe8
    {false, 5, "JMP     rel16/32"}            , // 0xe9
    {false, 0, "0xea"}, // 0xea
    {false, 2, "JMP     rel8"}                , // 0xeb
    {false, 0, "0xec"}, // 0xec
    {false, 0, "0xed"}, // 0xed
    {false, 0, "0xee"}, // 0xee
    {false, 0, "0xef"}, // 0xef
    {false, 0, "0xf0"}, // 0xf0
    {false, 0, "0xf1"}, // 0xf1
    {false, 0, "0xf2"}, // 0xf2
    {false, 1, "REP" }, // 0xf3
    {false, 0, "0xf4"}, // 0xf4
    {false, 0, "0xf5"}, // 0xf5
    {false, 0, "0xf6"}, // 0xf6
    {false, 0, "0xf7"}, // 0xf7
    {false, 0, "0xf8"}, // 0xf8
    {false, 0, "0xf9"}, // 0xf9
    {false, 0, "0xfa"}, // 0xfa
    {false, 0, "0xfb"}, // 0xfb
    {false, 0, "0xfc"}, // 0xfc
    {false, 0, "0xfd"}, // 0xfd
    {false, 0, "0xfe"}, // 0xfe
    {false, 0, "0xff"}, // 0xff
};

opcode_t s_2bytes_opcodes[256] =
{
    {false, 0, "0x00"}, // 0x00
    {false, 0, "0x01"}, // 0x01
    {false, 0, "0x02"}, // 0x02
    {false, 0, "0x03"}, // 0x03
    {false, 0, "0x04"}, // 0x04
    {false, 0, "0x05"}, // 0x05
    {false, 0, "0x06"}, // 0x06
    {false, 0, "0x07"}, // 0x07
    {false, 0, "0x08"}, // 0x08
    {false, 0, "0x09"}, // 0x09
    {false, 0, "0x0a"}, // 0x0a
    {false, 0, "0x0b"}, // 0x0b
    {false, 0, "0x0c"}, // 0x0c
    {false, 0, "0x0d"}, // 0x0d
    {false, 0, "0x0e"}, // 0x0e
    {false, 0, "0x0f"}, // 0x0f
    {false, 0, "0x10"}, // 0x10
    {false, 0, "0x11"}, // 0x11
    {false, 0, "0x12"}, // 0x12
    {false, 0, "0x13"}, // 0x13
    {false, 0, "0x14"}, // 0x14
    {false, 0, "0x15"}, // 0x15
    {false, 0, "0x16"}, // 0x16
    {false, 0, "0x17"}, // 0x17
    {false, 0, "0x18"}, // 0x18
    {false, 0, "0x19"}, // 0x19
    {false, 0, "0x1a"}, // 0x1a
    {false, 0, "0x1b"}, // 0x1b
    {false, 0, "0x1c"}, // 0x1c
    {false, 0, "0x1d"}, // 0x1d
    {false, 0, "0x1e"}, // 0x1e
    {false, 0, "0x1f"}, // 0x1f
    {false, 0, "0x20"}, // 0x20
    {false, 0, "0x21"}, // 0x21
    {false, 0, "0x22"}, // 0x22
    {false, 0, "0x23"}, // 0x23
    {false, 0, "0x24"}, // 0x24
    {false, 0, "0x25"}, // 0x25
    {false, 0, "0x26"}, // 0x26
    {false, 0, "0x27"}, // 0x27
    {false, 0, "0x28"}, // 0x28
    {false, 0, "0x29"}, // 0x29
    {false, 0, "0x2a"}, // 0x2a
    {false, 0, "0x2b"}, // 0x2b
    {false, 0, "0x2c"}, // 0x2c
    {false, 0, "0x2d"}, // 0x2d
    {false, 0, "0x2e"}, // 0x2e
    {false, 0, "0x2f"}, // 0x2f
    {false, 0, "0x30"}, // 0x30 
    {false, 0, "0x31"}, // 0x31 
    {false, 0, "0x32"}, // 0x32 
    {false, 0, "0x33"}, // 0x33 
    {false, 0, "0x34"}, // 0x34 
    {false, 0, "0x35"}, // 0x35
    {false, 0, "0x36"}, // 0x36
    {false, 0, "0x37"}, // 0x37
    {false, 0, "0x38"}, // 0x38
    {false, 0, "0x39"}, // 0x39
    {false, 0, "0x3a"}, // 0x3a
    {false, 0, "0x3b"}, // 0x3b
    {false, 0, "0x3c"}, // 0x3c
    {false, 0, "0x3d"}, // 0x3d
    {false, 0, "0x3e"}, // 0x3e
    {false, 0, "0x3f"}, // 0x3f
    {false, 0, "0x40"}, // 0x40 
    {false, 0, "0x41"}, // 0x41 
    {false, 0, "0x42"}, // 0x42 
    {false, 0, "0x43"}, // 0x43 
    {false, 0, "0x44"}, // 0x44 
    {false, 0, "0x45"}, // 0x45 
    {false, 0, "0x46"}, // 0x46 
    {false, 0, "0x47"}, // 0x47 
    {false, 0, "0x48"}, // 0x48
    {false, 0, "0x49"}, // 0x49
    {false, 0, "0x4a"}, // 0x4a
    {false, 0, "0x4b"}, // 0x4b
    {false, 0, "0x4c"}, // 0x4c
    {false, 0, "0x4d"}, // 0x4d
    {false, 0, "0x4e"}, // 0x4e
    {false, 0, "0x4f"}, // 0x4f
    {false, 0, "0x50"}, // 0x50
    {false, 0, "0x51"}, // 0x51
    {false, 0, "0x52"}, // 0x52
    {false, 0, "0x53"}, // 0x53
    {false, 0, "0x54"}, // 0x54
    {false, 0, "0x55"}, // 0x55
    {false, 0, "0x56"}, // 0x56
    {false, 0, "0x57"}, // 0x57
    {false, 0, "0x58"}, // 0x58
    {false, 0, "0x59"}, // 0x59
    {false, 0, "0x5a"}, // 0x5a
    {false, 0, "0x5b"}, // 0x5b
    {false, 0, "0x5c"}, // 0x5c
    {false, 0, "0x5d"}, // 0x5d
    {false, 0, "0x5e"}, // 0x5e
    {false, 0, "0x5f"}, // 0x5f
    {false, 0, "0x60"}, // 0x60
    {false, 0, "0x61"}, // 0x61
    {false, 0, "0x62"}, // 0x62
    {false, 0, "0x63"}, // 0x63
    {false, 0, "0x64"}, // 0x64
    {false, 0, "0x65"}, // 0x65
    {false, 0, "0x66"}, // 0x66
    {false, 0, "0x67"}, // 0x67
    {false, 0, "0x68"}, // 0x68
    {false, 0, "0x69"}, // 0x69
    {false, 0, "0x6a"}, // 0x6a
    {false, 0, "0x6b"}, // 0x6b
    {false, 0, "0x6c"}, // 0x6c
    {false, 0, "0x6d"}, // 0x6d
    {false, 0, "0x6e"}, // 0x6e
    {false, 0, "0x6f"}, // 0x6f
    {false, 0, "0x70"}, // 0x70
    {false, 0, "0x71"}, // 0x71
    {false, 0, "0x72"}, // 0x72
    {false, 0, "0x73"}, // 0x73
    {false, 0, "0x74"}, // 0x74
    {false, 0, "0x75"}, // 0x75
    {false, 0, "0x76"}, // 0x76
    {false, 0, "0x77"}, // 0x77
    {false, 0, "0x78"}, // 0x78
    {false, 0, "0x79"}, // 0x79
    {false, 0, "0x7a"}, // 0x7a
    {false, 0, "0x7b"}, // 0x7b
    {false, 0, "0x7c"}, // 0x7c
    {false, 0, "0x7d"}, // 0x7d
    {false, 0, "0x7e"}, // 0x7e
    {false, 0, "0x7f"}, // 0x7f
    {false, 0, "0x80"}, // 0x80 
    {false, 0, "0x81"}, // 0x81 
    {false, 0, "0x82"}, // 0x82 
    {false, 0, "0x83"}, // 0x83 
    {false, 0, "0x84"}, // 0x84 
    {false, 0, "0x85"}, // 0x85 
    {false, 0, "0x86"}, // 0x86 
    {false, 0, "0x87"}, // 0x87 
    {false, 0, "0x88"}, // 0x88 
    {false, 0, "0x89"}, // 0x89 
    {false, 0, "0x8a"}, // 0x8a 
    {false, 0, "0x8b"}, // 0x8b
    {false, 0, "0x8c"}, // 0x8c
    {false, 0, "0x8d"}, // 0x8d
    {false, 0, "0x8e"}, // 0x8e
    {false, 0, "0x8f"}, // 0x8f
    {false, 0, "0x90"}, // 0x90
    {false, 0, "0x91"}, // 0x91
    {false, 0, "0x92"}, // 0x92
    {false, 0, "0x93"}, // 0x93
    {false, 0, "0x94"}, // 0x94
    {false, 0, "0x95"}, // 0x95
    {false, 0, "0x96"}, // 0x96
    {false, 0, "0x97"}, // 0x97
    {false, 0, "0x98"}, // 0x98
    {false, 0, "0x99"}, // 0x99
    {false, 0, "0x9a"}, // 0x9a
    {false, 0, "0x9b"}, // 0x9b
    {false, 0, "0x9c"}, // 0x9c
    {false, 0, "0x9d"}, // 0x9d
    {false, 0, "0x9e"}, // 0x9e
    {false, 0, "0x9f"}, // 0x9f
    {false, 1, "PUSH   FS"}, // 0xa0
    {false, 1, "POP   FS"}, // 0xa1
    {false, 0, "0xa2"}, // 0xa2
    {false, 0, "0xa3"}, // 0xa3
    {false, 0, "0xa4"}, // 0xa4
    {false, 0, "0xa5"}, // 0xa5
    {false, 0, "0xa6"}, // 0xa6
    {false, 0, "INVALID"}   , // 0xa7
    {false, 0, "INVALID"}   , // 0xa8
    {false, 0, "0xa9"}, // 0xa9
    {false, 0, "0xaa"}, // 0xaa
    {false, 0, "0xab"}, // 0xab
    {false, 0, "0xac"}, // 0xac
    {false, 0, "0xad"}, // 0xad
    {false, 0, "0xae"}, // 0xae
    {false, 0, "0xaf"}, // 0xaf
    {false, 0, "0xb0"}, // 0xb0
    {false, 0, "0xb1"}, // 0xb1
    {false, 0, "0xb2"}, // 0xb2
    {false, 0, "0xb3"}, // 0xb3
    {false, 0, "0xb4"}, // 0xb4
    {false, 0, "0xb5"}, // 0xb5
    {false, 0, "0xb6"}, // 0xb6
    {false, 0, "0xb7"}, // 0xb7
    {false, 0, "0xb8"}, // 0xb8
    {false, 0, "0xb9"}, // 0xb9
    {false, 0, "0xba"}, // 0xba
    {false, 0, "0xbb"}, // 0xbb
    {false, 0, "0xbc"}, // 0xbc
    {false, 0, "0xbd"}, // 0xbd
    {false, 0, "0xbe"}, // 0xbe
    {false, 0, "0xbf"}, // 0xbf
    {false, 0, "0xc0"}, // 0xc0
    {false, 0, "0xc1"}, // 0xc1
    {false, 0, "0xc2"}, // 0xc2
    {false, 0, "0xc3"}, // 0xc3
    {false, 0, "0xc4"}, // 0xc4
    {false, 0, "0xc5"}, // 0xc5
    {false, 0, "0xc6"}, // 0xc6
    {false, 0, "0xc7"}, // 0xc7
    {false, 0, "0xc8"}, // 0xc8
    {false, 0, "0xc9"}, // 0xc9
    {false, 0, "0xca"}, // 0xca
    {false, 0, "0xcb"}, // 0xcb
    {false, 0, "0xcc"}, // 0xcc
    {false, 0, "0xcd"}, // 0xcd
    {false, 0, "0xce"}, // 0xce
    {false, 0, "0xcf"}, // 0xcf
    {false, 0, "0xd0"}, // 0xd0
    {false, 0, "0xd1"}, // 0xd1
    {false, 0, "0xd2"}, // 0xd2
    {false, 0, "0xd3"}, // 0xd3
    {false, 0, "0xd4"}, // 0xd4
    {false, 0, "0xd5"}, // 0xd5
    {false, 0, "0xd6"}, // 0xd6
    {false, 0, "0xd7"}, // 0xd7
    {false, 0, "0xd8"}, // 0xd8
    {false, 0, "0xd9"}, // 0xd9
    {false, 0, "0xda"}, // 0xda
    {false, 0, "0xdb"}, // 0xdb
    {false, 0, "0xdc"}, // 0xdc
    {false, 0, "0xdd"}, // 0xdd
    {false, 0, "0xde"}, // 0xde
    {false, 0, "0xdf"}, // 0xdf
    {false, 0, "0xe0"}, // 0xe0
    {false, 0, "0xe1"}, // 0xe1
    {false, 0, "0xe2"}, // 0xe2
    {false, 0, "0xe3"}, // 0xe3
    {false, 0, "0xe4"}, // 0xe4
    {false, 0, "0xe5"}, // 0xe5
    {false, 0, "0xe6"}, // 0xe6
    {false, 0, "0xe7"}, // 0xe7
    {false, 0, "0xe8"}, // 0xe8
    {false, 0, "0xe9"}, // 0xe9
    {false, 0, "0xea"}, // 0xea
    {false, 0, "0xeb"}, // 0xeb
    {false, 0, "0xec"}, // 0xec
    {false, 0, "0xed"}, // 0xed
    {false, 0, "0xee"}, // 0xee
    {false, 0, "0xef"}, // 0xef
    {false, 0, "0xf0"}, // 0xf0
    {false, 0, "0xf1"}, // 0xf1
    {false, 0, "0xf2"}, // 0xf2
    {false, 0, "0xf3"}, // 0xf3
    {false, 0, "0xf4"}, // 0xf4
    {false, 0, "0xf5"}, // 0xf5
    {false, 0, "0xf6"}, // 0xf6
    {false, 0, "0xf7"}, // 0xf7
    {false, 0, "0xf8"}, // 0xf8
    {false, 0, "0xf9"}, // 0xf9
    {false, 0, "0xfa"}, // 0xfa
    {false, 0, "0xfb"}, // 0xfb
    {false, 0, "0xfc"}, // 0xfc
    {false, 0, "0xfd"}, // 0xfd
    {false, 0, "0xfe"}, // 0xfe
    {false, 0, "0xff"}, // 0xff
};

static constexpr auto mod_mask = 0xC0;
static constexpr auto register_addressing_mode = 0xC0;
static constexpr auto four_bytes_signed_displacement = 0x80;
static constexpr auto one_byte_signed_displacement = 0x40;

static constexpr auto rm_mask = 0x07;
static constexpr auto displacement_only_addressing = 0x05;
static constexpr auto sib_with_no_displacement = 0x04;
static constexpr auto register_indirect_addressing_mode = 0x00;

#endif // MINI_DETOUR_X86_H
