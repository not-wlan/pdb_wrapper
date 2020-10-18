use snafu::Snafu;
use std::collections::HashMap;
use std::ffi::{c_void, CString, NulError};

pub mod pdb_meta;
mod pdb_wrapper;
use crate::pdb_meta::{CallingConvention, SimpleTypeKind};
use crate::pdb_wrapper::*;
use std::hash::{Hash, Hasher};
use std::io::Write;

#[derive(Debug, Snafu)]
pub enum Error {
    #[snafu(display("Bad symbol name, names may not contain null bytes!"))]
    BadSymbolName,
    #[snafu(display(
        "LLVM failed to generate a valid PDB, please double check all paths or file a bug!"
    ))]
    LLVMError,
    #[snafu(display("Tried to create a complex type!"))]
    BadType { ty: String },
    #[snafu(display("Unknown type {} was used!", ty))]
    UnknownType { ty: String },
}

#[derive(Debug, Clone)]
pub enum PDBType {
    Pointer(Box<PDBType>),
    SimpleType(SimpleTypeKind),
    Struct(String),
    ConstantArray(Box<PDBType>, usize),
    FunctionPointer {
        ret: Box<PDBType>,
        args: Vec<PDBType>,
        cconv: CallingConvention,
    },
}

#[derive(Debug)]
pub struct StructField {
    pub ty: PDBType,
    pub name: String,
    pub offset: u64,
}

impl Eq for PDBType {}

impl Hash for PDBType {
    fn hash<H: Hasher>(&self, state: &mut H) {
        std::mem::discriminant(self).hash(state);

        match self {
            PDBType::Pointer(ptr) => {
                (**ptr).hash(state);
            }
            PDBType::SimpleType(ty) => {
                ty.hash(state);
            }
            PDBType::Struct(name) => {
                name.hash(state);
            }
            PDBType::ConstantArray(array, size) => {
                (**array).hash(state);
                size.hash(state);
            }
            PDBType::FunctionPointer { ret, args, cconv } => {
                (**ret).hash(state);
                args.hash(state);
                cconv.hash(state);
            }
        };
    }
}

impl PartialEq for PDBType {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (PDBType::Struct(first), PDBType::Struct(second)) => first == second,
            (PDBType::SimpleType(first), PDBType::SimpleType(second)) => *first == *second,
            (PDBType::Pointer(first), PDBType::Pointer(second)) => (**first).eq(second.as_ref()),
            (_, _) => false,
        }
    }
}

impl From<NulError> for Error {
    fn from(_: NulError) -> Self {
        Error::BadSymbolName
    }
}

pub struct PDB {
    /// An instance of `pdb_file` stored as a void pointer
    handle: *mut c_void,
    /// Cache type indices by name
    pub(crate) types: HashMap<PDBType, u32>,
}

impl Drop for PDB {
    fn drop(&mut self) {
        unsafe { PDB_File_Destroy(self.handle) };
        self.handle = std::ptr::null_mut();
    }
}

impl PDB {
    pub fn new(is_64bit: bool) -> Result<Self, Error> {
        // I use integers to represent booleans on the FFI boundary.
        // Booleans *probably* work but I don't care to find out if this is true for every platform.
        let handle = unsafe { PDB_File_Create(if is_64bit { 1i32 } else { 0i32 }) };

        if handle.is_null() {
            return Err(Error::LLVMError);
        }

        Ok(PDB {
            handle,
            types: HashMap::new(),
        })
    }

    pub fn insert_global(
        &mut self,
        name: &str,
        section_index: u16,
        section_rva: u32,
        ty: Option<PDBType>,
    ) -> Result<(), Error> {
        let name = CString::new(name)?;
        if let Some(ty) = ty {
            let ty = self.get_or_create_type(&ty)?;
            unsafe {
                PDB_File_Add_Typed_Global(
                    self.handle,
                    name.as_ptr(),
                    section_index,
                    section_rva,
                    ty,
                )
            }
        } else {
            unsafe { PDB_File_Add_Global(self.handle, name.as_ptr(), section_index, section_rva) }
        }

        Ok(())
    }

    pub fn insert_function(
        &mut self,
        section_index: u16,
        section_rva: u32,
        name: &str,
        ty: Option<u32>,
    ) -> Result<(), Error> {
        let raw_name = CString::new(name)?;

        unsafe {
            match ty {
                None => PDB_File_Add_Function(
                    self.handle,
                    raw_name.as_ptr(),
                    section_index,
                    section_rva,
                ),
                Some(ty) => PDB_File_Add_Typed_Function(
                    self.handle,
                    raw_name.as_ptr(),
                    section_index,
                    section_rva,
                    ty,
                ),
            }
        }

        Ok(())
    }

    fn is_existing_type(&self, ty: &PDBType) -> bool {
        if let PDBType::SimpleType(_) = ty {
            return true;
        }

        self.types.contains_key(ty)
    }

    fn get_existing_type(&self, ty: &PDBType) -> Option<u32> {
        if !self.is_existing_type(ty) {
            return None;
        }

        if let PDBType::SimpleType(ty) = ty {
            return Some(*ty as u32);
        }

        self.types.get(ty).cloned()
    }

    fn create_type(&mut self, ty: &PDBType) -> Result<u32, Error> {
        match ty {
            PDBType::Pointer(inner) => {
                let type_index = match inner.as_ref() {
                    PDBType::SimpleType(ty) => Ok(*ty as u32),
                    PDBType::Pointer(ptr) if self.is_existing_type(inner.as_ref()) => Ok(self
                        .get_existing_type(ptr.as_ref())
                        .expect("Inconsistent types, this is a bug!")),
                    PDBType::Struct(name) => self
                        .get_existing_type(inner.as_ref())
                        .ok_or(Error::UnknownType { ty: name.clone() }),
                    PDBType::Pointer(_) => self.get_or_create_type(inner),
                    PDBType::ConstantArray(_, _) => {
                        unimplemented!("Pointer to array isn't supported yet!");
                    }
                    PDBType::FunctionPointer { .. } => {
                        unimplemented!("Pointer to function pointer isn't supported yet!");
                    }
                }?;

                let new_type = unsafe { PDB_File_Add_Pointer(self.handle, type_index) };
                self.types.insert(ty.clone(), new_type);
                Ok(new_type)
            }
            PDBType::ConstantArray(array, size) => {
                let ty = self.get_or_create_type(array.as_ref())?;
                return Ok(unsafe { PDB_File_Add_Array(self.handle, ty, *size as u64) });
            }
            PDBType::FunctionPointer { ret, args, cconv } => {
                let func = self.insert_function_metadata(ret, &args, false, *cconv, "")?;
                let new_type = unsafe { PDB_File_Add_Pointer(self.handle, func) };
                self.types.insert(ty.clone(), new_type);
                Ok(new_type)
            }
            _ => Err(Error::BadType {
                ty: format!("{:?}", ty),
            }),
        }
    }

    fn get_or_create_type(&mut self, ty: &PDBType) -> Result<u32, Error> {
        if let Some(ty) = self.get_existing_type(ty) {
            return Ok(ty);
        }

        self.create_type(ty)
    }

    pub fn insert_function_metadata(
        &mut self,
        return_type: &PDBType,
        args: &[PDBType],
        is_constructor: bool,
        cconv: pdb_meta::CallingConvention,
        name: &str,
    ) -> Result<u32, Error> {
        let raw_name = CString::new(name)?;

        let return_type = self.get_or_create_type(return_type)?;

        let args = args
            .iter()
            .map(|ty| self.get_or_create_type(ty))
            .collect::<Result<Vec<_>, Error>>()?;

        Ok(unsafe {
            PDB_File_Add_Func_Data(
                self.handle,
                raw_name.as_ptr(),
                return_type,
                args.as_ptr(),
                args.len() as u64,
                cconv as u8,
                if is_constructor { 1 } else { 0 },
            )
        })
    }

    pub fn insert_struct(
        &mut self,
        name: &str,
        fields: &[StructField],
        size: u64,
    ) -> Result<(), Error> {
        let field_list = unsafe { PDB_File_Field_List_Create() };
        let raw_name = CString::new(name)?;

        std::io::stdout().flush().unwrap();
        for field in fields {
            let ty = self.get_or_create_type(&field.ty)?;
            let raw_name = CString::new(field.name.as_str())?;
            unsafe { PDB_File_Field_List_Add(field_list, ty, field.offset, raw_name.as_ptr()) };
        }

        let ty = unsafe { PDB_File_Field_List_Finalize(self.handle, field_list) };
        let ty = unsafe {
            PDB_File_Create_Struct(
                self.handle,
                raw_name.as_ptr(),
                ty,
                fields.len() as u16,
                size,
            )
        };

        self.types.insert(PDBType::Struct(name.to_string()), ty);

        Ok(())
    }

    pub fn commit(&mut self, binary: &str, output: &str) -> Result<(), Error> {
        let raw_binary = CString::new(binary)?;
        let raw_output = CString::new(output)?;

        match unsafe { PDB_File_Commit(self.handle, raw_binary.as_ptr(), raw_output.as_ptr()) } {
            1 => Ok(()),
            _ => Err(Error::LLVMError),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::PDB;
    use pdb::FallibleIterator;
    use std::path::PathBuf;

    #[test]
    fn test_generation() {
        let mut basedir = PathBuf::new();
        basedir.push(env!("CARGO_MANIFEST_DIR"));
        basedir.push("test");
        basedir.push("HelloWorld.exe");

        let mut pdb = PDB::new(false).expect("Failed to create PDB instance.");
        pdb.insert_global("TestSymbol", 1, 0x1337, None)
            .expect("Failed to add symbol.");

        let pdbdir = format!("{}.pdb", basedir.as_path().to_string_lossy());
        let exedir = format!("{}", basedir.as_path().to_string_lossy());
        pdb.commit(&exedir, &pdbdir).expect("Failed to write PDB!");
        let file = std::fs::File::open(&pdbdir).expect("Failed to open test PDB!");
        let mut test_pdb = pdb::PDB::open(file).expect("Failed to open test PDB!");

        let symbol_table = test_pdb
            .global_symbols()
            .expect("Failed to get symbol table!");

        assert_eq!(symbol_table.iter().count().unwrap(), 1);

        let mut symbols = symbol_table.iter();
        while let Some(symbol) = symbols.next().unwrap() {
            match symbol.parse() {
                Ok(pdb::SymbolData::Public(data)) => {
                    assert_eq!(data.offset.offset, 0x1337);
                    assert_eq!(data.offset.section, 1);
                }
                _ => {
                    panic!("Bad symbol encoding");
                }
            }
        }
    }
}
