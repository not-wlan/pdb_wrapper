#include <llvm/DebugInfo/MSF/MSFBuilder.h>
#include <llvm/DebugInfo/PDB/Native/PDBFileBuilder.h>
#include <llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h>
#include <llvm/DebugInfo/PDB/Native/RawConstants.h>
#include <llvm/DebugInfo/CodeView/SymbolRecord.h>
#include <llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h>
#include <llvm/Object/Binary.h>
#include <llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h>
#include <llvm/DebugInfo/CodeView/ContinuationRecordBuilder.h>
#include <llvm/Object/COFF.h>
#include <llvm/DebugInfo/PDB/Native/TpiHashing.h>
#include <llvm/DebugInfo/CodeView/SymbolSerializer.h>

#include "wrapper.hpp"

#include <memory>
#include <vector>

using namespace llvm::codeview;

class pdb_file {
    std::unique_ptr<llvm::pdb::PDBFileBuilder> m_pdb_builder;
    std::unique_ptr<AppendingTypeTableBuilder> m_type_builder;
    std::unique_ptr<AppendingTypeTableBuilder> m_id_builder;
#if LLVM_VERSION_MAJOR > 10
    std::vector<llvm::pdb::BulkPublic> m_PublicsSyms;
#endif
    bool m_64_bit{};
public:
    pdb_file();

    bool initialize(bool is_64bit = false, uint32_t Age = 0,uint32_t Signature = 0,llvm::codeview::GUID OurGUID = llvm::codeview::GUID());

    void add_function_symbol(const char *name, uint16_t section_index, uint32_t section_offset);

    void add_global_symbol(const char *name, uint16_t section_index, uint32_t section_offset);
    void add_global_symbol(const char *name, uint16_t section_index, uint32_t section_offset, TypeIndex type);

    bool commit(const char *InputPath, const char *OutputPath);


    static ContinuationRecordBuilder *create_field_list();
    static void delete_field_list(ContinuationRecordBuilder * contBuilder);


    TypeIndex finalize_field_list(ContinuationRecordBuilder *cbr);

    TypeIndex
    add_struct(const char *name, TypeIndex fields, uint16_t fieldCount, uint64_t size);

    static void add_field(ContinuationRecordBuilder *cbr, TypeIndex type, uint64_t offset,
                          const char *name);


    TypeIndex add_function_data(const char *Name, TypeIndex return_type,
                                const std::vector<TypeIndex> &args, CallingConvention cconv,
                                bool is_constructor);

    TypeIndex add_pointer(TypeIndex type);

    TypeIndex add_array_type(TypeIndex type, uint64_t size);

    void add_function_symbol(const char *name, uint16_t section_index, uint32_t section_offset, TypeIndex fntype);

    std::unique_ptr<llvm::BumpPtrAllocator> m_allocator;

    void finalize_public_symbols();


};

pdb_file::pdb_file() {
    m_allocator = std::make_unique<llvm::BumpPtrAllocator>();
    m_pdb_builder = std::make_unique<llvm::pdb::PDBFileBuilder>(*m_allocator);
    m_type_builder = std::make_unique<AppendingTypeTableBuilder>(*m_allocator);
    m_id_builder = std::make_unique<AppendingTypeTableBuilder>(*m_allocator);
#if LLVM_VERSION_MAJOR > 10
    m_PublicsSyms = std::vector<llvm::pdb::BulkPublic>();
#endif
}

bool pdb_file::initialize(bool is_64bit ,uint32_t Age,uint32_t Signature,llvm::codeview::GUID OurGuid) {
    if (m_pdb_builder->initialize(4096)) {
        return false;
    }

    for (int i = 0; i < llvm::pdb::kSpecialStreamCount; ++i) {
        if (m_pdb_builder->getMsfBuilder().addStream(0).takeError()) {
            return false;
        }
    }

    m_64_bit = is_64bit;

    // Add an Info stream.
    auto &InfoBuilder = m_pdb_builder->getInfoBuilder();
    InfoBuilder.setAge(Age);
    InfoBuilder.setVersion(llvm::pdb::PdbRaw_ImplVer::PdbImplVC70);
    InfoBuilder.setHashPDBContentsToGUID(false);
    InfoBuilder.setSignature(Signature);
    InfoBuilder.setGuid(OurGuid);
    //Add an empty DBI stream.
    auto &DbiBuilder = m_pdb_builder->getDbiBuilder();
    DbiBuilder.setAge(InfoBuilder.getAge());
    DbiBuilder.setVersionHeader(llvm::pdb::PdbDbiV70);

    const auto machine = is_64bit ? llvm::COFF::MachineTypes::IMAGE_FILE_MACHINE_I386
                                  : llvm::COFF::MachineTypes::IMAGE_FILE_MACHINE_AMD64;
    DbiBuilder.setMachineType(machine);
    DbiBuilder.setFlags(llvm::pdb::DbiFlags::FlagHasCTypesMask);

    DbiBuilder.setBuildNumber(0x1337);

    // Technically we are not link.exe 14.11, but there are known cases where
    // debugging tools on Windows expect Microsoft-specific version numbers or
    // they fail to work at all.  Since we know we produce PDBs that are
    // compatible with LINK 14.11, we set that version number here.
    DbiBuilder.setBuildNumber(14, 11);

    auto &TpiBuilder = m_pdb_builder->getTpiBuilder();
    TpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

    auto &IpiBuilder = m_pdb_builder->getIpiBuilder();
    IpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

    return true;
}


bool pdb_file::commit(const char *InputPath, const char *OutputPath) {
    auto &DbiBuilder = m_pdb_builder->getDbiBuilder();
    auto binary = llvm::object::createBinary(InputPath);

    if (binary.takeError()) {
        return false;
    }

    auto object = llvm::dyn_cast<llvm::object::COFFObjectFile>((*binary).getBinary());

    auto section_count = object->getNumberOfSections();
    auto section_table = object->getCOFFSection(*object->sections().begin());

    auto sections = llvm::ArrayRef<llvm::object::coff_section>(section_table, section_count);

    // Add Section Map stream.
#if LLVM_VERSION_MAJOR > 10
    DbiBuilder.createSectionMap(sections);
#else
    auto sectionMap = llvm::pdb::DbiStreamBuilder::createSectionMap(sections);
    DbiBuilder.setSectionMap(sectionMap);
#endif

    auto raw_sections_table = llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(sections.begin()),
                                                      reinterpret_cast<const uint8_t *>(sections.end()));

    if (DbiBuilder.addDbgStream(llvm::pdb::DbgHeaderType::SectionHdr, raw_sections_table)) {
        return false;
    }

    auto &TpiBuilder = m_pdb_builder->getTpiBuilder();
    auto &IpiBuilder = m_pdb_builder->getIpiBuilder();

    m_type_builder->ForEachRecord([&](TypeIndex TI, CVType Type) {
        auto Hash = llvm::pdb::hashTypeRecord(Type);
        TpiBuilder.addTypeRecord(Type.RecordData, *Hash);
    });

    m_id_builder->ForEachRecord([&](TypeIndex TI, CVType Type) {
        auto Hash = llvm::pdb::hashTypeRecord(Type);
        IpiBuilder.addTypeRecord(Type.RecordData, *Hash);
    });

    auto &InfoBuilder = m_pdb_builder->getInfoBuilder();
    auto guid = InfoBuilder.getGuid();

    return !m_pdb_builder->commit(OutputPath, &guid);
}

void
pdb_file::add_function_symbol(const char *name, uint16_t section_index, uint32_t section_offset, TypeIndex fntype) {
    add_function_symbol(name, section_index, section_offset);
    auto proc = ProcSym(SymbolRecordKind::GlobalProcSym);
    auto frameproc = FrameProcSym(SymbolRecordKind::FrameProcSym);
    auto end = ScopeEndSym(SymbolRecordKind::ScopeEndSym);
    proc.Name = name;
    proc.Segment = section_index;
    proc.CodeOffset = section_offset;
    proc.FunctionType = fntype;

    auto &DbiBuilder = m_pdb_builder->getDbiBuilder();
    // This is toxic
    static auto &mod = DbiBuilder.addModuleInfo("llvm-pdb-wrapper.o").get();

    auto cvsym = SymbolSerializer::writeOneSymbol(proc, *m_allocator, CodeViewContainer::Pdb);
    auto cvsym_frame = SymbolSerializer::writeOneSymbol(frameproc, *m_allocator, CodeViewContainer::Pdb);
    auto cvsym_end = SymbolSerializer::writeOneSymbol(end, *m_allocator, CodeViewContainer::Pdb);
    auto &GsiBuilder = m_pdb_builder->getGsiBuilder();
    mod.addSymbol(cvsym);
    mod.addSymbol(cvsym_frame);
    mod.addSymbol(cvsym_end);
}

void pdb_file::add_function_symbol(const char *name, uint16_t section_index, uint32_t section_offset) {
    auto &GsiBuilder = m_pdb_builder->getGsiBuilder();
#if LLVM_VERSION_MAJOR > 10
    auto symbol = llvm::pdb::BulkPublic();
#else
    auto symbol = PublicSym32(SymbolKind::S_PUB32);
#endif

    symbol.Name = name;
#if LLVM_VERSION_MAJOR > 10
    symbol.Flags |= static_cast<uint16_t>(PublicSymFlags::Function);
#else
    symbol.Flags |= PublicSymFlags::Function;
#endif

    symbol.Segment = section_index;
    symbol.Offset = section_offset;
#if LLVM_VERSION_MAJOR > 10
    m_PublicsSyms.push_back(symbol);
#else
    GsiBuilder.addPublicSymbol(symbol);
#endif
}

void pdb_file::add_global_symbol(const char *name, uint16_t section_index, uint32_t section_offset, TypeIndex typeIndex) {
    add_global_symbol(name, section_index, section_offset);

    auto symbol = DataSym(SymbolKind::S_GDATA32);
    symbol.Name = name;
    symbol.Segment = section_index;
    symbol.DataOffset = section_offset;
    symbol.Type = typeIndex;

    auto &DbiBuilder = m_pdb_builder->getDbiBuilder();
    // This is toxic
    static auto &mod = DbiBuilder.addModuleInfo("globals.o").get();

    auto cvsym = SymbolSerializer::writeOneSymbol(symbol, *m_allocator, CodeViewContainer::Pdb);

    mod.addSymbol(cvsym);
}

void pdb_file::add_global_symbol(const char *name, uint16_t section_index, uint32_t section_offset) {
    auto &GsiBuilder = m_pdb_builder->getGsiBuilder();
#if LLVM_VERSION_MAJOR > 10
    auto symbol = llvm::pdb::BulkPublic();
#else
    auto symbol = PublicSym32(SymbolKind::S_PUB32);
#endif
    symbol.Name = name;
    symbol.Segment = section_index;
    symbol.Offset = section_offset;
#if LLVM_VERSION_MAJOR > 10
    symbol.Flags |= static_cast<uint16_t>(PublicSymFlags::Function | PublicSymFlags::Code);
    m_PublicsSyms.push_back(symbol);
#else
    symbol.Flags |= PublicSymFlags::Function | PublicSymFlags::Code;
    GsiBuilder.addPublicSymbol(symbol);
#endif
}

#if LLVM_VERSION_MAJOR > 10
void pdb_file::finalize_public_symbols() {
    auto &GsiBuilder = m_pdb_builder->getGsiBuilder();
    GsiBuilder.addPublicSymbols(std::move(m_PublicsSyms));
}
#endif

void pdb_file::delete_field_list(ContinuationRecordBuilder * contBuilder) {
    delete contBuilder;
}

ContinuationRecordBuilder *pdb_file::create_field_list() {
    auto contBuilder = new ContinuationRecordBuilder();
    contBuilder->begin(ContinuationRecordKind::FieldList);
    return contBuilder;
}

TypeIndex pdb_file::add_pointer(TypeIndex type) {
    auto ptr_record = PointerRecord(
            type, PointerKind::Near32, PointerMode::Pointer, PointerOptions::None, 4
    );
    return m_type_builder->writeLeafType(ptr_record);
}

TypeIndex pdb_file::add_array_type(TypeIndex type, uint64_t size) {
    // TODO: Why do array records need names?
    auto array_record = ArrayRecord(type, TypeIndex(m_64_bit ? SimpleTypeKind::Int64 : SimpleTypeKind::Int32), size,
                                    "");

    return m_type_builder->writeLeafType(array_record);
}


void pdb_file::add_field(ContinuationRecordBuilder *cbr, TypeIndex type, uint64_t offset, const char *name) {
    auto record = DataMemberRecord();
    record.Name = name;
    record.FieldOffset = offset;
    record.Type = type;
    record.Kind = TypeRecordKind::DataMember;

    cbr->writeMemberType(record);
}

TypeIndex pdb_file::add_function_data(const char *Name, TypeIndex return_type, const std::vector<TypeIndex> &args,
                                      CallingConvention cconv, bool is_constructor) {
    auto arglist = ArgListRecord(
            TypeRecordKind::ArgList, args
    );
    auto arglist_index = m_type_builder->writeLeafType(arglist);
    auto record = ProcedureRecord(return_type, cconv,
                                  is_constructor ? FunctionOptions::Constructor : FunctionOptions::None, args.size(),
                                  arglist_index);
    auto func_type = m_type_builder->writeLeafType(record);

    auto func_id = FuncIdRecord(TypeIndex(0), func_type, Name);

    m_id_builder->writeLeafType(func_id);

    return func_type;
}

TypeIndex pdb_file::finalize_field_list(ContinuationRecordBuilder *cbr) {
    cbr->end(m_type_builder->nextTypeIndex());
    auto index = m_type_builder->insertRecord(*cbr);
    delete cbr;
    return index;
}

TypeIndex pdb_file::add_struct(const char *name, TypeIndex fields, uint16_t fieldCount, uint64_t size) {

    assert(m_type_builder->getType(fields).kind() == TypeLeafKind::LF_FIELDLIST);

    m_type_builder->getType(fields);
    auto classRecord = ClassRecord(TypeRecordKind::Struct, fieldCount, ClassOptions::None, fields, TypeIndex::None(),
                                   TypeIndex::None(), size, name, name);

    return m_type_builder->writeLeafType(classRecord);
}


#if LLVM_VERSION_MAJOR > 10
EXPORT void *PDB_File_Create(int Is64Bit,uint32_t Age,uint32_t Signature,uint8_t const* GUIDData) {
    auto pdb = new pdb_file();
    auto guid = llvm::codeview::GUID();
    memcpy(guid.Guid, GUIDData, 16);
    if (!pdb->initialize(!!Is64Bit,Age,Signature,guid)) {
        delete pdb;
        return nullptr;
    }
    return pdb;
}
#else
EXPORT void *PDB_File_Create(int Is64Bit) {
    auto pdb = new pdb_file();
    if (!pdb->initialize(!!Is64Bit)) {
        delete pdb;
        return nullptr;
    }
    return pdb;
}
#endif

EXPORT void PDB_File_Add_Typed_Function(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset,
                                        uint32_t Type) {
    auto pdb = (pdb_file *) Instance;
    pdb->add_function_symbol(Name, SectionIndex, SectionOffset, TypeIndex(Type));
}

EXPORT void PDB_File_Add_Function(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset) {
    auto pdb = (pdb_file *) Instance;
    pdb->add_function_symbol(Name, SectionIndex, SectionOffset);
}

EXPORT void PDB_File_Add_Typed_Global(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset, uint32_t Type) {
    auto pdb = (pdb_file *) Instance;
    pdb->add_global_symbol(Name, SectionIndex, SectionOffset, TypeIndex(Type));
}

EXPORT void PDB_File_Add_Global(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset) {
    auto pdb = (pdb_file *) Instance;
    pdb->add_global_symbol(Name, SectionIndex, SectionOffset);
}

EXPORT void PDB_File_Destroy(void *Instance) {
    delete (pdb_file *) Instance;
}

EXPORT int PDB_File_Commit(void *Instance, const char *InputPath, const char *OutputPath) {
    auto pdb = (pdb_file *) Instance;
#if LLVM_VERSION_MAJOR > 10
    pdb->finalize_public_symbols();
#endif
    return +pdb->commit(InputPath, OutputPath);
}

EXPORT void PDB_File_Field_List_Destroy(void* Builder) {
    return pdb_file::delete_field_list((ContinuationRecordBuilder*)Builder);
}

EXPORT void *PDB_File_Field_List_Create() {
    return pdb_file::create_field_list();
}

EXPORT void PDB_File_Field_List_Add(void *CRBInstance, uint32_t Type, uint64_t Offset, const char *Name) {
    const auto type = TypeIndex{Type};
    auto crb = (ContinuationRecordBuilder *) CRBInstance;
    pdb_file::add_field(crb, type, Offset, Name);
}

EXPORT uint32_t

PDB_File_Field_List_Finalize(void *Instance, void *CRBInstance) {
    auto crb = (ContinuationRecordBuilder *) CRBInstance;
    auto pdb = (pdb_file *) Instance;
    return pdb->finalize_field_list(crb).getIndex();
}

EXPORT uint32_t PDB_File_Create_Struct(void *Instance, const char *Name, uint32_t Fields, uint16_t FieldCount, uint64_t Size) {
    const auto type = TypeIndex{Fields};
    auto pdb = (pdb_file *) Instance;
    return pdb->add_struct(Name, type, FieldCount, Size).getIndex();
}

EXPORT uint32_t PDB_File_Add_Func_Data(void *Instance, const char *Name, uint32_t ReturnType, const uint32_t *Args,
                       const size_t ArgCount, uint8_t CConv, int IsConstructor) {
    auto pdb = (pdb_file *) Instance;
    const auto return_type = TypeIndex{ReturnType};
    auto types = std::vector<TypeIndex>{};

    types.reserve(ArgCount);

    for (int i = 0; i < ArgCount; ++i) {
        types.emplace_back(TypeIndex(Args[i]));
    }

    return pdb->add_function_data(Name, return_type, types, (CallingConvention) CConv, !!IsConstructor).getIndex();
}

EXPORT uint32_t PDB_File_Add_Pointer(void *Instance, uint32_t Type) {
    const auto type = TypeIndex{Type};
    auto pdb = (pdb_file *) Instance;
    return pdb->add_pointer(type).getIndex();
}

EXPORT uint32_t PDB_File_Add_Array(void *Instance, uint32_t Type, uint64_t Size) {
    const auto type = TypeIndex{Type};
    auto pdb = (pdb_file *) Instance;
    return pdb->add_array_type(type, Size).getIndex();
}