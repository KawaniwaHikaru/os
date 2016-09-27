/*++

Copyright (c) 2016 Minoca Corp. All Rights Reserved

Module Name:

    module.c

Abstract:

    This module implements support for module in Chalk.

Author:

    Evan Green 29-May-2016

Environment:

    Any

--*/

//
// ------------------------------------------------------------------- Includes
//

#include "chalkp.h"
#include "compiler.h"

//
// ---------------------------------------------------------------- Definitions
//

//
// ------------------------------------------------------ Data Type Definitions
//

//
// ----------------------------------------------- Internal Function Prototypes
//

BOOL
CkpModuleRun (
    PCK_VM Vm,
    PCK_VALUE Arguments
    );

BOOL
CkpModuleGetVariable (
    PCK_VM Vm,
    PCK_VALUE Arguments
    );

BOOL
CkpModuleToString (
    PCK_VM Vm,
    PCK_VALUE Arguments
    );

//
// -------------------------------------------------------------------- Globals
//

CK_PRIMITIVE_DESCRIPTION CkModulePrimitives[] = {
    {"run@0", 0, CkpModuleRun},
    {"get@1", 1, CkpModuleGetVariable},
    {"__str@0", 0, CkpModuleToString},
    {NULL, 0, NULL}
};

//
// ------------------------------------------------------------------ Functions
//

CK_VALUE
CkpModuleLoad (
    PCK_VM Vm,
    CK_VALUE ModuleName
    )

/*++

Routine Description:

    This routine loads the given module.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    ModuleName - Supplies the module name value.

Return Value:

    Returns the newly loaded module value on success.

    CK_NULL_VALUE on failure.

--*/

{

    CK_LOAD_MODULE_RESULT LoadStatus;
    PCK_MODULE Module;
    CK_MODULE_HANDLE ModuleData;
    PCK_STRING NameString;
    CK_VALUE PathValue;
    CK_VALUE Value;

    CK_ASSERT(CK_IS_STRING(ModuleName));

    NameString = CK_AS_STRING(ModuleName);
    PathValue = CkNullValue;

    //
    // If the module already exists, just return it.
    //

    Module = CkpModuleGet(Vm, ModuleName);
    if (Module != NULL) {
        CK_OBJECT_VALUE(Value, Module);
        return Value;
    }

    if (Vm->Configuration.LoadModule == NULL) {
        CkpRuntimeError(Vm, "ImportError", "Module load not supported");
        return CkNullValue;
    }

    //
    // Call out to the big city to actually go get the module.
    //

    LoadStatus = Vm->Configuration.LoadModule(Vm,
                                              NameString->Value,
                                              &ModuleData);

    switch (LoadStatus) {

    //
    // The module came back as a source file.
    //

    case CkLoadModuleSource:
        if (ModuleData.Source.PathLength != 0) {
            PathValue = CkpStringCreate(Vm,
                                        ModuleData.Source.Path,
                                        ModuleData.Source.PathLength);

            CkFree(Vm, ModuleData.Source.Path);
        }

        Module = CkpModuleLoadSource(Vm,
                                     ModuleName,
                                     PathValue,
                                     ModuleData.Source.Text,
                                     ModuleData.Source.Length);

        CkFree(Vm, ModuleData.Source.Text);
        if (Module == NULL) {
            CkpRuntimeError(Vm,
                            "CompileError",
                            "Module compile error: %s",
                            NameString->Value);

            return CkNullValue;
        }

        break;

    //
    // The module came back as a foreign library.
    //

    case CkLoadModuleForeign:
        if (ModuleData.Foreign.PathLength != 0) {
            PathValue = CkpStringCreate(Vm,
                                        ModuleData.Foreign.Path,
                                        ModuleData.Foreign.PathLength);

            CkFree(Vm, ModuleData.Source.Path);
        }

        Module = CkpModuleLoadForeign(Vm,
                                      ModuleName,
                                      PathValue,
                                      ModuleData.Foreign.Handle,
                                      ModuleData.Foreign.Entry);

        //
        // If module creation failed, unload the dynamic library.
        //

        if (Module == NULL) {
            if (ModuleData.Foreign.Handle != NULL) {
                if (Vm->Configuration.UnloadForeignModule != NULL) {
                    Vm->Configuration.UnloadForeignModule(
                                                    ModuleData.Foreign.Handle);
                }
            }
        }

        break;

    //
    // Some details occurred, undoubtedly a disappointment to someone.
    //

    case CkLoadModuleNotFound:
        CkpRuntimeError(Vm, "ImportError", "Module not found");
        return CkNullValue;

    case CkLoadModuleNoMemory:
        if (!CK_IS_NULL(Vm->Fiber->Error)) {
            CkpRuntimeError(Vm, "MemoryError", "Allocation failure");
        }

        return CkNullValue;

    case CkLoadModuleNotSupported:
        CkpRuntimeError(Vm, "ImportError", "Module loading not supported");
        break;

    case CkLoadModuleStaticError:
    case CkLoadModuleFreeError:
        CkpRuntimeError(Vm, "ImportError", "%s", ModuleData.Error);
        if (LoadStatus == CkLoadModuleFreeError) {
            CkFree(Vm, ModuleData.Error);
        }

        break;

    default:
        CkpRuntimeError(Vm, "ImportError", "Unknown module load error");
        return CkNullValue;
    }

    CK_OBJECT_VALUE(Value, Module);
    return Value;
}

PCK_MODULE
CkpModuleLoadSource (
    PCK_VM Vm,
    CK_VALUE ModuleName,
    CK_VALUE Path,
    PSTR Source,
    UINTN Length
    )

/*++

Routine Description:

    This routine loads the given source under the given module name.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    ModuleName - Supplies the module name value.

    Path - Supplies an optional value that is the full path to the module.

    Source - Supplies a pointer to the null terminated string containing the
        source to interpret.

    Length - Supplies the length of the source string, not including the null
        terminator.

Return Value:

    Returns a pointer to the newly loaded module on success.

    NULL on failure.

--*/

{

    PCK_CLOSURE Closure;
    PCK_FUNCTION Function;
    PCK_MODULE Module;
    PCK_STRING PathString;

    PathString = NULL;
    if (CK_IS_STRING(Path)) {
        PathString = CK_AS_STRING(Path);
    }

    Module = CkpModuleGet(Vm, ModuleName);
    if (Module == NULL) {
        Module = CkpModuleCreate(Vm, CK_AS_STRING(ModuleName), PathString);
        if (Module == NULL) {
            return NULL;
        }
    }

    Function = CkpCompile(Vm, Module, Source, Length, TRUE);
    if (Function == NULL) {
        return NULL;
    }

    CkpPushRoot(Vm, &(Function->Header));
    Closure = CkpClosureCreate(Vm, Function, NULL);
    CkpPopRoot(Vm);
    if (Closure == NULL) {
        return NULL;
    }

    CkpPushRoot(Vm, &(Closure->Header));
    Module->Fiber = CkpFiberCreate(Vm, Closure);
    CkpPopRoot(Vm);
    return Module;
}

PCK_MODULE
CkpModuleLoadForeign (
    PCK_VM Vm,
    CK_VALUE ModuleName,
    CK_VALUE Path,
    PVOID Handle,
    PCK_FOREIGN_FUNCTION EntryPoint
    )

/*++

Routine Description:

    This routine loads a new foreign module.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    ModuleName - Supplies the module name value.

    Path - Supplies an optional value that is the full path to the module.

    Handle - Supplies a handle to the opened dynamic library.

    EntryPoint - Supplies a pointer to the C function to call to load the
        module.

Return Value:

    Returns a pointer to the newly loaded module on success.

    NULL on failure.

--*/

{

    PCK_CLOSURE Closure;
    PCK_STRING FunctionName;
    CK_VALUE FunctionNameValue;
    PCK_MODULE Module;
    PCK_STRING PathString;

    PathString = NULL;
    if (CK_IS_STRING(Path)) {
        PathString = CK_AS_STRING(Path);
    }

    //
    // Multiple foreign modules shouldn't be loaded at the same module.
    //

    CK_ASSERT(CkpModuleGet(Vm, ModuleName) == NULL);

    Module = CkpModuleCreate(Vm, CK_AS_STRING(ModuleName), PathString);
    if (Module == NULL) {
        return NULL;
    }

    FunctionNameValue = CkpStringCreate(Vm, CK_MODULE_ENTRY_NAME, 12);
    FunctionName = NULL;
    if (!CK_IS_NULL(FunctionNameValue)) {
        FunctionName = CK_AS_STRING(FunctionNameValue);
    }

    Closure = CkpClosureCreateForeign(Vm,
                                      EntryPoint,
                                      Module,
                                      FunctionName,
                                      0);

    if (Closure == NULL) {
        return NULL;
    }

    Module->EntryFunction = Closure;
    return Module;
}

PCK_MODULE
CkpModuleCreate (
    PCK_VM Vm,
    PCK_STRING Name,
    PCK_STRING Path
    )

/*++

Routine Description:

    This routine allocates and initializes new module object.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Name - Supplies a pointer to the module name.

    Path - Supplies an optional pointer to the complete module path.

Return Value:

    Returns a pointer to the newly created module on success.

    NULL on allocation failure.

--*/

{

    PCK_MODULE CoreModule;
    CK_SYMBOL_INDEX Index;
    PCK_MODULE Module;
    CK_VALUE NameValue;
    PCK_STRING String;
    CK_VALUE Value;

    Module = CkpModuleAllocate(Vm, Name, Path);
    if (Module == NULL) {
        return NULL;
    }

    CK_OBJECT_VALUE(NameValue, Name);
    CK_OBJECT_VALUE(Value, Module);
    CkpDictSet(Vm, Vm->Modules, NameValue, Value);

    //
    // Load up the core module an add all its variables into the current
    // namespace.
    //

    CoreModule = CkpModuleGet(Vm, CK_NULL_VALUE);
    for (Index = 0; Index < CoreModule->Variables.Count; Index += 1) {
        String = CK_AS_STRING(CoreModule->VariableNames.List.Data[Index]);
        CkpDefineModuleVariable(Vm,
                                Module,
                                String->Value,
                                String->Length,
                                CoreModule->Variables.Data[Index]);
    }

    return Module;
}

PCK_MODULE
CkpModuleAllocate (
    PCK_VM Vm,
    PCK_STRING Name,
    PCK_STRING Path
    )

/*++

Routine Description:

    This routine allocates a new module object.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Name - Supplies a pointer to the module name.

    Path - Supplies an optional pointer to the complete module path.

Return Value:

    Returns a pointer to the newly allocated module on success.

    NULL on allocation failure.

--*/

{

    CK_ERROR_TYPE Error;
    PCK_MODULE Module;

    if (Name != NULL) {
        CkpPushRoot(Vm, &(Name->Header));
    }

    if (Path != NULL) {
        CkpPushRoot(Vm, &(Path->Header));
    }

    Module = CkAllocate(Vm, sizeof(CK_MODULE));
    if (Name != NULL) {
        CkpPopRoot(Vm);
    }

    if (Path != NULL) {
        CkpPopRoot(Vm);
    }

    if (Module == NULL) {
        return NULL;
    }

    CkZero(Module, sizeof(CK_MODULE));
    CkpInitializeObject(Vm,
                        &(Module->Header),
                        CkObjectModule,
                        Vm->Class.Module);

    Module->Name = Name;
    Module->Path = Path;
    CkpPushRoot(Vm, &(Module->Header));
    Error = CkpStringTableInitialize(Vm, &(Module->VariableNames));
    if (Error != CkSuccess) {
        Module = NULL;
        goto ModuleCreateEnd;
    }

    Error = CkpStringTableInitialize(Vm, &(Module->Strings));
    if (Error != CkSuccess) {
        Module = NULL;
        goto ModuleCreateEnd;
    }

    CkpInitializeArray(&(Module->Variables));

ModuleCreateEnd:
    CkpPopRoot(Vm);
    return Module;
}

PCK_MODULE
CkpModuleGet (
    PCK_VM Vm,
    CK_VALUE Name
    )

/*++

Routine Description:

    This routine attempts to find a previously loaded module.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Name - Supplies the module name value.

Return Value:

    Returns a pointer to the module with the given name on success.

    NULL if no such module exists.

--*/

{

    CK_VALUE Module;

    Module = CkpDictGet(Vm->Modules, Name);
    if (CK_IS_UNDEFINED(Module)) {
        return NULL;
    }

    return CK_AS_MODULE(Module);
}

VOID
CkpModuleDestroy (
    PCK_VM Vm,
    PCK_MODULE Module
    )

/*++

Routine Description:

    This routine is called when a module object is being destroyed.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Module - Supplies a pointer to the dying module.

Return Value:

    None.

--*/

{

    //
    // Allow the dynamic library to get unloaded if this is a foreign module.
    //

    if (Module->Handle != NULL) {
        if (Vm->Configuration.UnloadForeignModule != NULL) {
            Vm->Configuration.UnloadForeignModule(Module->Handle);
        }

        Module->Handle = NULL;
    }

    return;
}

//
// --------------------------------------------------------- Internal Functions
//

//
// Module class primitives
//

BOOL
CkpModuleRun (
    PCK_VM Vm,
    PCK_VALUE Arguments
    )

/*++

Routine Description:

    This routine executes the contents of a module, if not already run.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Arguments - Supplies the function arguments.

Return Value:

    TRUE on success.

    FALSE if execution caused a runtime error.

--*/

{

    PCK_CLOSURE EntryFunction;
    PCK_FIBER Fiber;
    PCK_MODULE Module;

    Module = CK_AS_MODULE(Arguments[0]);
    Fiber = Module->Fiber;
    if (Fiber == NULL) {

        //
        // See if there's an entry function, and run that (once) if so.
        //

        EntryFunction = Module->EntryFunction;
        if (EntryFunction != NULL) {
            Module->EntryFunction = NULL;
            CkpCallFunction(Vm, EntryFunction, 1);

        } else {
            Arguments[0] = CkNullValue;
        }

        return TRUE;
    }

    //
    // Clear out and switch to the module contents fiber.
    //

    CK_ASSERT((Fiber->FrameCount != 0) && (CK_IS_NULL(Fiber->Error)));

    Module->Fiber = NULL;
    Fiber->Caller = Vm->Fiber;
    Vm->Fiber = Fiber;

    //
    // Return false to indicate a fiber switch.
    //

    return FALSE;
}

BOOL
CkpModuleGetVariable (
    PCK_VM Vm,
    PCK_VALUE Arguments
    )

/*++

Routine Description:

    This routine returns a module level variable.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Arguments - Supplies the function arguments.

Return Value:

    TRUE on success.

    FALSE if execution caused a runtime error.

--*/

{

    PCK_MODULE Module;
    PCK_STRING Name;
    PCK_VALUE Variable;

    Module = CK_AS_MODULE(Arguments[0]);
    if (!CK_IS_STRING(Arguments[1])) {
        CkpRuntimeError(Vm, "TypeError", "Expected a string");
        return FALSE;
    }

    Name = CK_AS_STRING(Arguments[1]);
    Variable = CkpFindModuleVariable(Vm, Module, Name->Value, FALSE);
    if (Variable == NULL) {
        CkpRuntimeError(Vm,
                        "NameError",
                        "No such variable '%s' in module '%s'",
                        Name->Value,
                        Module->Name->Value);

        return FALSE;
    }

    Arguments[0] = *Variable;
    return TRUE;
}

BOOL
CkpModuleToString (
    PCK_VM Vm,
    PCK_VALUE Arguments
    )

/*++

Routine Description:

    This routine returns the string representation of the given module.

Arguments:

    Vm - Supplies a pointer to the virtual machine.

    Arguments - Supplies the function arguments.

Return Value:

    TRUE on success.

    FALSE if execution caused a runtime error.

--*/

{

    PCK_MODULE Module;
    PCSTR Name;
    PCSTR Path;

    Module = CK_AS_MODULE(Arguments[0]);
    Name = "<builtin>";
    Path = NULL;
    if (Module->Name != NULL) {
        Name = Module->Name->Value;
    }

    if (Module->Path != NULL) {
        Path = Module->Path->Value;
    }

    if (Path != NULL) {
        Arguments[0] = CkpStringFormat(Vm,
                                       "<module \"$\" at \"$\">",
                                       Name,
                                       Path);

    } else {
        Arguments[0] = CkpStringFormat(Vm, "<module \"$\">", Name);
    }

    return TRUE;
}

