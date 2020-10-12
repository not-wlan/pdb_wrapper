#[repr(C)]
#[allow(dead_code)]
#[derive(Debug, PartialOrd, PartialEq, Hash, Copy, Clone)]
pub enum SimpleTypeKind {
    None = 0x0000,          // uncharacterized type (no type)
    Void = 0x0003,          // void
    NotTranslated = 0x0007, // type not translated by cvpack
    HResult = 0x0008,       // OLE/COM HRESULT

    SignedCharacter = 0x0010,   // 8 bit signed
    UnsignedCharacter = 0x0020, // 8 bit unsigned
    NarrowCharacter = 0x0070,   // really a char
    WideCharacter = 0x0071,     // wide char
    Character16 = 0x007a,       // char16_t
    Character32 = 0x007b,       // char32_t

    SByte = 0x0068,       // 8 bit signed int
    Byte = 0x0069,        // 8 bit unsigned int
    Int16Short = 0x0011,  // 16 bit signed
    UInt16Short = 0x0021, // 16 bit unsigned
    Int16 = 0x0072,       // 16 bit signed int
    UInt16 = 0x0073,      // 16 bit unsigned int
    Int32Long = 0x0012,   // 32 bit signed
    UInt32Long = 0x0022,  // 32 bit unsigned
    Int32 = 0x0074,       // 32 bit signed int
    UInt32 = 0x0075,      // 32 bit unsigned int
    Int64Quad = 0x0013,   // 64 bit signed
    UInt64Quad = 0x0023,  // 64 bit unsigned
    Int64 = 0x0076,       // 64 bit signed int
    UInt64 = 0x0077,      // 64 bit unsigned int
    Int128Oct = 0x0014,   // 128 bit signed int
    UInt128Oct = 0x0024,  // 128 bit unsigned int
    Int128 = 0x0078,      // 128 bit signed int
    UInt128 = 0x0079,     // 128 bit unsigned int

    Float16 = 0x0046,                 // 16 bit real
    Float32 = 0x0040,                 // 32 bit real
    Float32PartialPrecision = 0x0045, // 32 bit PP real
    Float48 = 0x0044,                 // 48 bit real
    Float64 = 0x0041,                 // 64 bit real
    Float80 = 0x0042,                 // 80 bit real
    Float128 = 0x0043,                // 128 bit real

    Complex16 = 0x0056,                 // 16 bit complex
    Complex32 = 0x0050,                 // 32 bit complex
    Complex32PartialPrecision = 0x0055, // 32 bit PP complex
    Complex48 = 0x0054,                 // 48 bit complex
    Complex64 = 0x0051,                 // 64 bit complex
    Complex80 = 0x0052,                 // 80 bit complex
    Complex128 = 0x0053,                // 128 bit complex

    Boolean8 = 0x0030,   // 8 bit boolean
    Boolean16 = 0x0031,  // 16 bit boolean
    Boolean32 = 0x0032,  // 32 bit boolean
    Boolean64 = 0x0033,  // 64 bit boolean
    Boolean128 = 0x0034, // 128 bit boolean
}

/// These values correspond to the CV_call_e enumeration, and are documented
/// at the following locations:
///   https://msdn.microsoft.com/en-us/library/b2fc64ek.aspx
///   https://msdn.microsoft.com/en-us/library/windows/desktop/ms680207(v=vs.85).aspx
///
#[repr(C)]
#[allow(dead_code)]
#[derive(Debug, PartialOrd, PartialEq, Hash, Copy, Clone)]
pub enum CallingConvention {
    NearC = 0x00,       // near right to left push, caller pops stack
    FarC = 0x01,        // far right to left push, caller pops stack
    NearPascal = 0x02,  // near left to right push, callee pops stack
    FarPascal = 0x03,   // far left to right push, callee pops stack
    NearFast = 0x04,    // near left to right push with regs, callee pops stack
    FarFast = 0x05,     // far left to right push with regs, callee pops stack
    NearStdCall = 0x07, // near standard call
    FarStdCall = 0x08,  // far standard call
    NearSysCall = 0x09, // near sys call
    FarSysCall = 0x0a,  // far sys call
    ThisCall = 0x0b,    // this call (this passed in register)
    MipsCall = 0x0c,    // Mips call
    Generic = 0x0d,     // Generic call sequence
    AlphaCall = 0x0e,   // Alpha call
    PpcCall = 0x0f,     // PPC call
    SHCall = 0x10,      // Hitachi SuperH call
    ArmCall = 0x11,     // ARM call
    AM33Call = 0x12,    // AM33 call
    TriCall = 0x13,     // TriCore Call
    SH5Call = 0x14,     // Hitachi SuperH-5 call
    M32RCall = 0x15,    // M32R Call
    ClrCall = 0x16,     // clr call
    Inline = 0x17,      // Marker for routines always inlined and thus lacking a convention
    NearVector = 0x18,  // near left to right push with regs, callee pops stack
}
