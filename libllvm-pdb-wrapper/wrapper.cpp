#include "wrapper.hpp"

#include <memory>

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


class pdb_file {
    std::unique_ptr<llvm::BumpPtrAllocator> m_allocator;
    std::unique_ptr<llvm::pdb::PDBFileBuilder> m_pdb_builder;
    std::unique_ptr<llvm::codeview::AppendingTypeTableBuilder> m_type_builder;
public:
    pdb_file();
    bool initialize(bool is_64bit = false);
    void add_function_symbol(const char* name, uint16_t section_index, uint32_t section_offset);
    void add_global_symbol(const char* name, uint16_t section_index, uint32_t section_offset);
    bool commit(const char* InputPath, const char* OutputPath);



    static llvm::codeview::ContinuationRecordBuilder *create_field_list();


    llvm::codeview::TypeIndex finalize_field_list(llvm::codeview::ContinuationRecordBuilder *cbr);

    llvm::codeview::TypeIndex
    add_struct(const char *name, llvm::codeview::TypeIndex fields, uint16_t fieldCount, uint64_t size);

    static void add_field(llvm::codeview::ContinuationRecordBuilder *cbr, llvm::codeview::TypeIndex type, uint64_t offset,
                   const char *name);


    llvm::codeview::TypeIndex add_function_data(const char *Name, llvm::codeview::TypeIndex return_type,
                                                const std::vector<llvm::codeview::TypeIndex> &args, llvm::codeview::CallingConvention cconv,
                                                bool is_constructor);

    llvm::codeview::TypeIndex add_pointer(llvm::codeview::TypeIndex type);
};

pdb_file::pdb_file() {
    m_allocator = std::make_unique<llvm::BumpPtrAllocator>();
    m_pdb_builder = std::make_unique<llvm::pdb::PDBFileBuilder>(*m_allocator);
    m_type_builder = std::make_unique<llvm::codeview::AppendingTypeTableBuilder>(*m_allocator);
}

bool pdb_file::initialize(bool is_64bit) {
    if(m_pdb_builder->initialize(4096)) {
        return false;
    }

    for(int i = 0; i < llvm::pdb::kSpecialStreamCount; ++i) {
        if(m_pdb_builder->getMsfBuilder().addStream(0).takeError()) {
            return false;
        }
    }

    // Add an Info stream.
    auto& InfoBuilder = m_pdb_builder->getInfoBuilder();
    InfoBuilder.setVersion(llvm::pdb::PdbRaw_ImplVer::PdbImplVC70);
    InfoBuilder.setHashPDBContentsToGUID(false);
    InfoBuilder.setAge(0);

    llvm::codeview::GUID guid{};
    InfoBuilder.setGuid(guid);

    //Add an empty DBI stream.
    auto& DbiBuilder = m_pdb_builder->getDbiBuilder();
    DbiBuilder.setAge(InfoBuilder.getAge());
    DbiBuilder.setVersionHeader(llvm::pdb::PdbDbiV70);

    const auto machine = is_64bit ?  llvm::COFF::MachineTypes::IMAGE_FILE_MACHINE_I386 : llvm::COFF::MachineTypes::IMAGE_FILE_MACHINE_AMD64;
    DbiBuilder.setMachineType(machine);
    DbiBuilder.setFlags(llvm::pdb::DbiFlags::FlagStrippedMask);

    // Technically we are not link.exe 14.11, but there are known cases where
    // debugging tools on Windows expect Microsoft-specific version numbers or
    // they fail to work at all.  Since we know we produce PDBs that are
    // compatible with LINK 14.11, we set that version number here.
    DbiBuilder.setBuildNumber(14, 11);

    auto& TpiBuilder = m_pdb_builder->getTpiBuilder();
    TpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);



    auto& IpiBuilder = m_pdb_builder->getIpiBuilder();
    IpiBuilder.setVersionHeader(llvm::pdb::PdbTpiV80);

    return true;
}



bool pdb_file::commit(const char* InputPath, const char* OutputPath) {
    auto& DbiBuilder = m_pdb_builder->getDbiBuilder();
    auto binary = llvm::object::createBinary(InputPath);

    if(binary.takeError()) {
        return false;
    }

    auto object = llvm::dyn_cast<llvm::object::COFFObjectFile>((*binary).getBinary());

    auto section_count = object->getNumberOfSections();
    auto section_table = object->getCOFFSection(*object->sections().begin());

    auto sections = llvm::ArrayRef<llvm::object::coff_section>(section_table, section_count);

    // Add Section Map stream.
    auto sectionMap =llvm::pdb::DbiStreamBuilder::createSectionMap(sections);
    DbiBuilder.setSectionMap(sectionMap);

    auto raw_sections_table = llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(sections.begin()), reinterpret_cast<const uint8_t*>(sections.end()));

    if(DbiBuilder.addDbgStream(llvm::pdb::DbgHeaderType::SectionHdr, raw_sections_table)) {
        return false;
    }

    auto& TpiBuilder = m_pdb_builder->getTpiBuilder();

    m_type_builder->ForEachRecord([&](llvm::codeview::TypeIndex TI, llvm::codeview::CVType Type) {
        auto Hash = llvm::pdb::hashTypeRecord(Type);
        TpiBuilder.addTypeRecord(Type.RecordData, *Hash);
    });

    auto& InfoBuilder = m_pdb_builder->getInfoBuilder();
    auto guid = InfoBuilder.getGuid();

    return !m_pdb_builder->commit(OutputPath, &guid);
}

void pdb_file::add_function_symbol(const char *name, uint16_t section_index, uint32_t section_offset) {
    auto& GsiBuilder = m_pdb_builder->getGsiBuilder();
    auto symbol = llvm::codeview::PublicSym32(llvm::codeview::SymbolKind::S_PUB32);

    symbol.Name = name;
    symbol.Flags |= llvm::codeview::PublicSymFlags::Function;
    symbol.Segment = section_index;
    symbol.Offset = section_offset;

    GsiBuilder.addPublicSymbol(symbol);
}

void pdb_file::add_global_symbol(const char *name, uint16_t section_index, uint32_t section_offset) {
    auto &GsiBuilder = m_pdb_builder->getGsiBuilder();
    auto symbol = llvm::codeview::PublicSym32(llvm::codeview::SymbolKind::S_PUB32);

    symbol.Name = name;
    symbol.Segment = section_index;
    symbol.Offset = section_offset;

    GsiBuilder.addPublicSymbol(symbol);
}

llvm::codeview::ContinuationRecordBuilder* pdb_file::create_field_list() {
    auto contBuilder = new llvm::codeview::ContinuationRecordBuilder();
    contBuilder->begin(llvm::codeview::ContinuationRecordKind::FieldList);
    return contBuilder;
}

llvm::codeview::TypeIndex pdb_file::add_pointer(llvm::codeview::TypeIndex type) {
    auto ptr_record = llvm::codeview::PointerRecord(
            type, llvm::codeview::PointerKind::Near32, llvm::codeview::PointerMode::Pointer, llvm::codeview::PointerOptions::None, 4
    );
    return m_type_builder->writeLeafType(ptr_record);
}

void pdb_file::add_field(llvm::codeview::ContinuationRecordBuilder* cbr, llvm::codeview::TypeIndex type, uint64_t offset, const char* name) {
    auto record = llvm::codeview::DataMemberRecord();
    record.Name = name;
    record.FieldOffset = offset;
    record.Type = type;
    record.Kind = llvm::codeview::TypeRecordKind::DataMember;

    cbr->writeMemberType(record);
}

llvm::codeview::TypeIndex pdb_file::add_function_data(const char* Name, llvm::codeview::TypeIndex return_type, const std::vector<llvm::codeview::TypeIndex>& args, llvm::codeview::CallingConvention cconv, bool is_constructor) {
    auto arglist = llvm::codeview::ArgListRecord(
        llvm::codeview::TypeRecordKind::ArgList, args
    );
    auto arglist_index = m_type_builder->writeLeafType(arglist);
    auto record = llvm::codeview::ProcedureRecord(return_type, cconv, is_constructor ? llvm::codeview::FunctionOptions::Constructor : llvm::codeview::FunctionOptions::None, args.size(), arglist_index);
    auto func_type = m_type_builder->writeLeafType(record);

    auto func_id = llvm::codeview::FuncIdRecord(llvm::codeview::TypeIndex(0), func_type, Name);
    return m_type_builder->writeLeafType(func_id);
}

llvm::codeview::TypeIndex pdb_file::finalize_field_list(llvm::codeview::ContinuationRecordBuilder* cbr) {
    cbr->end(m_type_builder->nextTypeIndex());
    auto index = m_type_builder->insertRecord(*cbr);
    delete cbr;
    return index;
}

llvm::codeview::TypeIndex pdb_file::add_struct(const char* name, llvm::codeview::TypeIndex fields, uint16_t fieldCount, uint64_t size) {

    assert(m_type_builder->getType(fields).kind() == llvm::codeview::TypeLeafKind::LF_FIELDLIST);

    m_type_builder->getType(fields);
    auto classRecord = llvm::codeview::ClassRecord(llvm::codeview::TypeRecordKind::Struct, fieldCount, llvm::codeview::ClassOptions::None, fields, llvm::codeview::TypeIndex::None(),llvm::codeview::TypeIndex::None(), size, name, name);

    return m_type_builder->writeLeafType(classRecord);
}

EXPORT void *PDB_File_Create(int Is64Bit)  {
    auto pdb = new pdb_file();
    if(!pdb->initialize(!!Is64Bit)) {
        delete pdb;
        return nullptr;
    }
    return pdb;
}

EXPORT void PDB_File_Add_Function(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset) {
    auto pdb = (pdb_file*)Instance;
    pdb->add_function_symbol(Name, SectionIndex, SectionOffset);
}

EXPORT void PDB_File_Add_Global(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset) {
    auto pdb = (pdb_file*)Instance;
    pdb->add_global_symbol(Name, SectionIndex, SectionOffset);
}

EXPORT void PDB_File_Destroy(void *Instance) {
    delete (pdb_file*)Instance;
}

EXPORT int PDB_File_Commit(void *Instance, const char* InputPath, const char* OutputPath) {
    auto pdb = (pdb_file*)Instance;
    return +pdb->commit(InputPath, OutputPath);
}

EXPORT void* PDB_File_Field_List_Create() {
    return pdb_file::create_field_list();
}

EXPORT void PDB_File_Field_List_Add(void* CRBInstance, uint32_t Type, uint64_t Offset, const char* Name) {
    const auto type = llvm::codeview::TypeIndex{Type};
    auto crb = (llvm::codeview::ContinuationRecordBuilder*)CRBInstance;
    pdb_file::add_field(crb, type, Offset, Name);
}

EXPORT uint32_t PDB_File_Field_List_Finalize(void* Instance, void* CRBInstance) {
    auto crb = (llvm::codeview::ContinuationRecordBuilder*)CRBInstance;
    auto pdb = (pdb_file*)Instance;
    return pdb->finalize_field_list(crb).getIndex();
}

EXPORT uint32_t PDB_File_Create_Struct(void *Instance, const char *Name, uint32_t Fields, uint16_t FieldCount, uint64_t Size) {
    const auto type = llvm::codeview::TypeIndex{Fields};
    auto pdb = (pdb_file*)Instance;
    return pdb->add_struct(Name, type, FieldCount, Size).getIndex();
}

EXPORT uint32_t PDB_File_Add_Func_Data(void* Instance, const char* Name, uint32_t ReturnType, const uint32_t* Args, const size_t ArgCount, uint8_t CConv, int IsConstructor) {
    auto pdb = (pdb_file*)Instance;
    const auto return_type = llvm::codeview::TypeIndex{ReturnType};
    auto types = std::vector<llvm::codeview::TypeIndex>{};

    types.reserve(ArgCount);

    for(int i = 0; i < ArgCount; ++i) {
        types.emplace_back(llvm::codeview::TypeIndex(Args[i]));
    }

    return pdb->add_function_data(Name,return_type, types, (llvm::codeview::CallingConvention)CConv, !!IsConstructor).getIndex();
}

EXPORT uint32_t PDB_File_Add_Pointer(void* Instance, uint32_t Type) {
    const auto type = llvm::codeview::TypeIndex{Type};
    auto pdb = (pdb_file*)Instance;
    return pdb->add_pointer(type).getIndex();
}
