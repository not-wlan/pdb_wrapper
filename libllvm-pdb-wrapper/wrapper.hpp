//
// Created by jan on 08.10.20.
//

#ifndef LLVM_PDB_WRAPPER_WRAPPER_HPP
#define LLVM_PDB_WRAPPER_WRAPPER_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#if defined(_MSC_VER)
//  Microsoft
    #define EXPORT __declspec(dllexport)
    #define IMPORT __declspec(dllimport)
#elif defined(__GNUC__)
//  GCC
#define EXPORT extern "C" __attribute__((visibility("default")))
#define IMPORT
#else
//  do nothing and hope for the best?
    #define EXPORT
    #define IMPORT
    #pragma warning Unknown dynamic link import/export semantics.
#endif

#if LLVM_VERSION_MAJOR > 10
    EXPORT void *PDB_File_Create(int Is64Bit,uint32_t Age,uint32_t Signature,uint8_t const *GUIDData);
#else
    EXPORT void* PDB_File_Create(int Is64Bit);
#endif

EXPORT void PDB_File_Add_Function(void* Instance, const char* Name, uint16_t SectionIndex, uint32_t SectionOffset);
EXPORT void PDB_File_Add_Global(void* Instance, const char* Name, uint16_t SectionIndex, uint32_t SectionOffset);
EXPORT int PDB_File_Commit(void *Instance, const char* InputPath, const char* OutputPath);
EXPORT void PDB_File_Destroy(void* Instance);
EXPORT void* PDB_File_Field_List_Create();
EXPORT void PDB_File_Field_List_Add(void* CRBInstance, uint32_t Type, uint64_t Offset, const char* Name);
EXPORT uint32_t PDB_File_Field_List_Finalize(void* Instance, void* CRBInstance);
EXPORT uint32_t PDB_File_Create_Struct(void *Instance, const char *Name, uint32_t Fields, uint16_t FieldCount, uint64_t Size);
EXPORT uint32_t PDB_File_Add_Func_Data(void* Instance, const char* Name, uint32_t ReturnType, const uint32_t* Args, size_t ArgCount, uint8_t CConv, int IsConstructor);
EXPORT uint32_t PDB_File_Add_Pointer(void* Instance, uint32_t Type);
EXPORT uint32_t PDB_File_Add_Array(void* Instance, uint32_t Type, uint64_t Size);
EXPORT void PDB_File_Add_Typed_Function(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset, uint32_t Type);
EXPORT void PDB_File_Add_Typed_Global(void *Instance, const char *Name, uint16_t SectionIndex, uint32_t SectionOffset, uint32_t TypeIndex);
#endif //LLVM_PDB_WRAPPER_WRAPPER_HPP
