// Copyright (c) 2008-2022 the Urho3D project
// License: MIT

#include "../Precompiled.h"

#include "../AngelScript/Script.h"
#include "../Core/Context.h"
#include "../IO/File.h"
#include "../IO/FileSystem.h"
#include "../IO/Log.h"

#include <AngelScript/angelscript.h>

#include "../DebugNew.h"

namespace Urho3D
{

/// %Object property info for scripting API dump.
struct PropertyInfo
{
    /// Construct.
    PropertyInfo() :
        read_(false),
        write_(false),
        indexed_(false)
    {
    }

    /// Property name.
    String name_;
    /// Property data type.
    String type_;
    /// Reading supported flag.
    bool read_;
    /// Writing supported flag.
    bool write_;
    /// Indexed flag.
    bool indexed_;
};

/// Header information for dumping events.
struct HeaderFile
{
    /// Full path to header file.
    String fileName;
    /// Event section name.
    String sectionName;
};

bool CompareHeaderFiles(const HeaderFile& lhs, const HeaderFile& rhs)
{
    return lhs.sectionName < rhs.sectionName;
}

void ExtractPropertyInfo(const String& functionName, const String& declaration, Vector<PropertyInfo>& propertyInfos)
{
    String propertyName = functionName.Substring(4);
    PropertyInfo* info = nullptr;
    for (unsigned k = 0; k < propertyInfos.Size(); ++k)
    {
        if (propertyInfos[k].name_ == propertyName)
        {
            info = &propertyInfos[k];
            break;
        }
    }
    if (!info)
    {
        propertyInfos.Resize(propertyInfos.Size() + 1);
        info = &propertyInfos.Back();
        info->name_ = propertyName;
    }
    if (functionName.Contains("get_"))
    {
        info->read_ = true;
        // Extract type from the return value
        Vector<String> parts = declaration.Split(' ');
        if (parts.Size())
        {
            if (parts[0] != "const")
                info->type_ = parts[0];
            else if (parts.Size() > 1)
                info->type_ = parts[1];
        }
        // If get method has parameters, it is indexed
        if (!declaration.Contains("()"))
        {
            info->indexed_ = true;
            info->type_ += "[]";
        }

        // Sanitate the reference operator away
        info->type_.Replace("&", "");
    }
    if (functionName.Contains("set_"))
    {
        info->write_ = true;
        if (info->type_.Empty())
        {
            // Extract type from parameters
            i32 begin = declaration.Find(',');
            if (begin == String::NPOS)
                begin = declaration.Find('(');
            else
                info->indexed_ = true;

            if (begin != String::NPOS)
            {
                ++begin;
                i32 end = declaration.Find(')');
                if (end != String::NPOS)
                {
                    info->type_ = declaration.Substring(begin, end - begin);
                    // Sanitate const & reference operator away
                    info->type_.Replace("const ", "");
                    info->type_.Replace("&in", "");
                    info->type_.Replace("&", "");
                }
            }
        }
    }
}

bool ComparePropertyStrings(const String& lhs, const String& rhs)
{
    i32 spaceLhs = lhs.Find(' ');
    i32 spaceRhs = rhs.Find(' ');
    if (spaceLhs != String::NPOS && spaceRhs != String::NPOS)
        return String::Compare(lhs.CString() + spaceLhs, rhs.CString() + spaceRhs, true) < 0;
    else
        return String::Compare(lhs.CString(), rhs.CString(), true) < 0;
}

bool ComparePropertyInfos(const PropertyInfo& lhs, const PropertyInfo& rhs)
{
    return String::Compare(lhs.name_.CString(), rhs.name_.CString(), true) < 0;
}

void Script::OutputAPIRow(DumpMode mode, const String& row, bool removeReference, const String& separator)
{
    String out(row.Trimmed());
    out.Replace("&in", "&");
    out.Replace("&out", "&");
    if (removeReference)
        out.Replace("&", "");

    if (mode == DOXYGEN)
        Log::WriteRaw("- " + out + "\n");
    else if (mode == C_HEADER)
    {
        out.Replace("@", "");
        out.Replace("?&", "void*");

        // s/(\w+)\[\]/Array<\1>/g
        i32 posBegin = String::NPOS;
        while (true)   // Loop to cater for array of array of T
        {
            i32 posEnd = out.Find("[]");
            if (posEnd == String::NPOS)
                break;
            if (posBegin == String::NPOS)
                posBegin = posEnd - 1;
            while (posBegin >= 0 && isalnum(out[posBegin]))
                --posBegin;
            ++posBegin;
            out.Replace(posBegin, posEnd - posBegin + 2, "Array<" + out.Substring(posBegin, posEnd - posBegin) + ">");
        }
        // "opIndex" early was replaced to "operator[ ]", so not replaced here to Array<operator>
        out.Replace("[ ]", "[]");

        Log::WriteRaw(out + separator + "\n");
    }
}

void Script::DumpAPI(DumpMode mode, const String& sourceTree)
{
    // Does not use URHO3D_LOGRAW macro here to ensure the messages are always dumped regardless of URHO3D_LOGGING compiler directive
    // and of Log subsystem availability

    // Dump event descriptions and attribute definitions in Doxygen mode. For events, this means going through the header files,
    // as the information is not available otherwise.
    /// \todo Dump events + attributes before the actual script API because the remarks (readonly / writeonly) seem to throw off
    // Doxygen parsing and the following page definition(s) may not be properly recognized
    if (mode == DOXYGEN)
    {
        Log::WriteRaw("namespace Urho3D\n{\n\n/**\n");

        auto* fileSystem = GetSubsystem<FileSystem>();
        Vector<String> headerFileNames;
        String path = AddTrailingSlash(sourceTree);
        if (!path.Empty())
            path.Append("Source/Urho3D/");

        fileSystem->ScanDir(headerFileNames, path, "*.h", SCAN_FILES, true);

        /// \hack Rename any Events2D to 2DEvents to work with the event category creation correctly (currently PhysicsEvents2D)
        Vector<HeaderFile> headerFiles;
        for (const String& headerFileName : headerFileNames)
        {
            HeaderFile entry;
            entry.fileName = headerFileName;
            entry.sectionName = GetFileNameAndExtension(entry.fileName).Replaced("Events2D", "2DEvents");
            if (entry.sectionName.EndsWith("Events.h"))
                headerFiles.Push(entry);
        }

        if (!headerFiles.Empty())
        {
            Log::WriteRaw("\n\\page EventList Event list\n");
            Sort(headerFiles.Begin(), headerFiles.End(), CompareHeaderFiles);

            for (const HeaderFile& headerFile : headerFiles)
            {
                SharedPtr<File> file(new File(context_, path + headerFile.fileName, FILE_READ));
                if (!file->IsOpen())
                    continue;

                const String& sectionName = headerFile.sectionName;
                i32 start = sectionName.Find('/') + 1;
                i32 end = sectionName.Find("Events.h");
                Log::WriteRaw("\n## %" + sectionName.Substring(start, end - start) + " events\n");

                while (!file->IsEof())
                {
                    String line = file->ReadLine();
                    if (line.StartsWith("URHO3D_EVENT"))
                    {
                        Vector<String> parts = line.Split(',');
                        if (parts.Size() == 2)
                            Log::WriteRaw("\n### " + parts[1].Substring(0, parts[1].Length() - 1).Trimmed() + "\n");
                    }
                    if (line.Contains("URHO3D_PARAM"))
                    {
                        Vector<String> parts = line.Split(',');
                        if (parts.Size() == 2)
                        {
                            String paramName = parts[1].Substring(0, parts[1].Find(')')).Trimmed();
                            String paramType = parts[1].Substring(parts[1].Find("// ") + 3);
                            if (!paramName.Empty() && !paramType.Empty())
                                Log::WriteRaw("- %" + paramName + " : " + paramType + "\n");
                        }
                    }
                }
            }

            Log::WriteRaw("\n");
        }

        Log::WriteRaw("\n\\page AttributeList Attribute list\n");

        const HashMap<StringHash, Vector<AttributeInfo>>& attributes = context_->GetAllAttributes();

        Vector<String> objectTypes;
        for (HashMap<StringHash, Vector<AttributeInfo>>::ConstIterator i = attributes.Begin(); i != attributes.End(); ++i)
            objectTypes.Push(context_->GetTypeName(i->first_));

        Sort(objectTypes.Begin(), objectTypes.End());

        for (const String& objectType : objectTypes)
        {
            const Vector<AttributeInfo>& attrs = attributes.Find(objectType)->second_;
            bool hasEditableAttr = false;
            for (const AttributeInfo& attr : attrs)
            {
                // Attributes that are not shown in the editor are typically internal and not usable for eg. attribute
                // animation
                if (!(attr.mode_ & AM_NOEDIT))
                {
                    hasEditableAttr = true;
                    break;
                }
            }

            if (!hasEditableAttr)
                continue;

            Log::WriteRaw("\n### " + objectType + "\n");

            for (const AttributeInfo& attr : attrs)
            {
                if (attr.mode_ & AM_NOEDIT)
                    continue;
                // Prepend each word in the attribute name with % to prevent unintended links
                Vector<String> nameParts = attr.name_.Split(' ');
                for (String& namePart : nameParts)
                {
                    if (namePart.Length() > 1 && IsAlpha((unsigned)namePart[0]))
                        namePart = "%" + namePart;
                }
                String name;
                name.Join(nameParts, " ");
                String type = Variant::GetTypeName(attr.type_);
                // Variant typenames are all uppercase. Convert primitive types to the proper lowercase form for the documentation
                if (type == "Int" || type == "Bool" || type == "Float")
                    type[0] = (char)ToLower((unsigned)type[0]);

                Log::WriteRaw("- " + name + " : " + type + "\n");
            }
        }

        Log::WriteRaw("\n");
    }

    if (mode == DOXYGEN)
        Log::WriteRaw("\n\\page ScriptAPI Scripting API\n\n");
    else if (mode == C_HEADER)
        Log::WriteRaw(
            "// Script API header intended to be 'force included' in IDE for AngelScript content assist / code completion\n\n"
                "#define int8 signed char\n"
                "#define int16 signed short\n"
                "#define int64 long\n"
                "#define uint unsigned\n"
                "#define uint8 unsigned char\n"
                "#define uint16 unsigned short\n"
                "#define uint64 unsigned long\n"
                "#define null 0\n"
                "#define in\n"
                "#define out\n"
                "#define inout\n"
                "#define is ==\n"
                "#define interface struct\n"
                "#define class struct\n"
                "#define cast reinterpret_cast\n"
                "#define mixin\n"
                "#define funcdef\n"
                "#define protected\n"
                "#define private\n"
        );

    unsigned types = scriptEngine_->GetObjectTypeCount();
    Vector<Pair<String, unsigned>> sortedTypes;
    for (unsigned i = 0; i < types; ++i)
    {
        asITypeInfo* type = scriptEngine_->GetObjectTypeByIndex(i);
        if (type)
        {
            String typeName(type->GetName());
            sortedTypes.Push(MakePair(typeName, i));
        }
    }
    Sort(sortedTypes.Begin(), sortedTypes.End());

    // Get global constants by namespace
    HashMap<String, Vector<String>> globalConstants;
    unsigned properties = scriptEngine_->GetGlobalPropertyCount();
    for (unsigned i = 0; i < properties; ++i)
    {
        const char* propertyName;
        const char* propertyDeclaration;
        const char* propertyNameSpace;
        int typeId;
        scriptEngine_->GetGlobalPropertyByIndex(i, &propertyName, &propertyNameSpace, &typeId);
        propertyDeclaration = scriptEngine_->GetTypeDeclaration(typeId);

        String type(propertyDeclaration);
        globalConstants[String(propertyNameSpace)].Push(type + " " + String(propertyName));
    }
    for (HashMap<String, Vector<String>>::Iterator i = globalConstants.Begin(); i != globalConstants.End(); ++i)
        Sort(i->second_.Begin(), i->second_.End(), ComparePropertyStrings);

    if (mode == DOXYGEN)
    {
        Log::WriteRaw("\\section ScriptAPI_TableOfContents Table of contents\n"
            "\\ref ScriptAPI_ClassList \"Class list\"<br>\n"
            "\\ref ScriptAPI_Classes \"Classes\"<br>\n"
            "\\ref ScriptAPI_Enums \"Enumerations\"<br>\n"
            "\\ref ScriptAPI_GlobalFunctions \"Global functions\"<br>\n"
            "\\ref ScriptAPI_GlobalProperties \"Global properties\"<br>\n"
            "\\ref ScriptAPI_GlobalConstants \"Global constants\"<br>\n\n");

        Log::WriteRaw("\\section ScriptAPI_ClassList Class list\n\n");

        for (unsigned i = 0; i < sortedTypes.Size(); ++i)
        {
            asITypeInfo* type = scriptEngine_->GetObjectTypeByIndex(sortedTypes[i].second_);
            if (type)
            {
                String typeName(type->GetName());
                Log::WriteRaw("<a href=\"#Class_" + typeName + "\"><b>" + typeName + "</b></a>\n");
            }
        }

        Log::WriteRaw("\n\\section ScriptAPI_Classes Classes\n");
    }
    else if (mode == C_HEADER)
        Log::WriteRaw("\n// Classes\n");

    for (unsigned i = 0; i < sortedTypes.Size(); ++i)
    {
        asITypeInfo* type = scriptEngine_->GetObjectTypeByIndex(sortedTypes[i].second_);
        if (type)
        {
            String typeName(type->GetName());
            Vector<String> methodDeclarations;
            Vector<PropertyInfo> propertyInfos;

            if (mode == DOXYGEN)
            {
                Log::WriteRaw("<a name=\"Class_" + typeName + "\"></a>\n");
                Log::WriteRaw("\n### " + typeName + "\n");
            }
            else if (mode == C_HEADER)
            {
                if (type->GetFlags() & asOBJ_TEMPLATE) {
                    String str = "\ntemplate <";
                    for (asUINT tt = 0, ttm = type->GetSubTypeCount(); tt < ttm; tt++) {
                        asITypeInfo* pSubType = type->GetSubType(tt);
                        str += String("typename ") + pSubType->GetName() + (tt < ttm - 1 ? ", " : ">");
                    }
                    Log::WriteRaw(str);
                }
                Log::WriteRaw("\nclass " + typeName + "\n{\npublic:\n");
                for (asUINT m = 0, mc = type->GetBehaviourCount(); m < mc; m++) {
                    asEBehaviours bh;
                    asIScriptFunction* pM = type->GetBehaviourByIndex(m, &bh);
                    if (bh == asBEHAVE_CONSTRUCT || bh == asBEHAVE_DESTRUCT)
                        Log::WriteRaw(String(pM->GetDeclaration(false, false, true)) + ";\n");
                }
                for (asUINT m = 0, mc = type->GetFactoryCount(); m < mc; m++) {
                    asIScriptFunction* pM = type->GetFactoryByIndex(m);
                    String declaration(pM->GetDeclaration(false, false, true));
                    declaration = declaration.Substring(declaration.Find(' ') + 1);
                    declaration.Replace("@", "&");
                    Log::WriteRaw(declaration + ";\n");
                }
                if (typeName == "String")
                    Log::WriteRaw("String(const char*);\n");
            }

            unsigned methods = type->GetMethodCount();
            for (unsigned j = 0; j < methods; ++j)
            {
                asIScriptFunction* method = type->GetMethodByIndex(j);
                String methodName(method->GetName());
                String declaration(method->GetDeclaration(true, false, true));

                // Recreate tab escape sequences
                declaration.Replace("\t", "\\t");

                if (methodName.Contains("get_") || methodName.Contains("set_"))
                    ExtractPropertyInfo(methodName, declaration, propertyInfos);
                else
                {
                    // Sanitate the method name. For some operators fix name
                    if (declaration.Contains("::op")) {
                        declaration.Replace("::opEquals(", ":: operator==(");
                        declaration.Replace("::opAssign(", ":: operator=(");
                        declaration.Replace("::opAddAssign(", ":: operator+=(");
                        declaration.Replace("::opAdd(", ":: operator+(");
                        declaration.Replace("::opCmp(", ":: operator<(");
                        declaration.Replace("::opPreInc(", ":: operator++(");
                        declaration.Replace("::opPostInc()", ":: operator++(int)");
                        declaration.Replace("::opIndex(", ":: operator[ ](");
                        if (declaration.Contains("::opImplCast()") || declaration.Contains("::opImplConv()")) {
                            int sp = declaration.Find(' ');
                            String retType = declaration.Substring(0, sp);
                            if (retType == "const")
                            {
                                sp = declaration.Find(' ', sp + 1);
                                retType = declaration.Substring(0, sp);
                            }
                            declaration = "operator " + retType + "() const";
                        }
                    }
                    if (!declaration.Contains("::op"))
                    {
                        String prefix(typeName + "::");
                        declaration.Replace(prefix, "");
                        ///\todo Is there a better way to mark deprecated API bindings for AngelScript?
                        i32 posBegin = declaration.FindLast("const String&in = \"deprecated:");
                        if (posBegin != String::NPOS)
                        {
                            // Assume this 'mark' is added as the last parameter
                            i32 posEnd = declaration.Find(')', posBegin);
                            if (posEnd != String::NPOS)
                            {
                                declaration.Replace(posBegin, posEnd - posBegin, "");
                                posBegin = declaration.Find(", ", posBegin - 2);
                                if (posBegin != String::NPOS)
                                    declaration.Replace(posBegin, 2, "");
                                if (mode == DOXYGEN)
                                    declaration += " // deprecated";
                                else if (mode == C_HEADER)
                                    declaration = "/* deprecated */\n" + declaration;
                            }
                        }
                        methodDeclarations.Push(declaration);
                    }
                }
            }

            // Assume that the same property is never both an accessor property, and a direct one
            unsigned properties = type->GetPropertyCount();
            for (unsigned j = 0; j < properties; ++j)
            {
                const char* propertyName;
                const char* propertyDeclaration;
                int typeId;

                type->GetProperty(j, &propertyName, &typeId);
                propertyDeclaration = scriptEngine_->GetTypeDeclaration(typeId);

                PropertyInfo newInfo;
                newInfo.name_ = String(propertyName);
                newInfo.type_ = String(propertyDeclaration);
                newInfo.read_ = newInfo.write_ = true;
                propertyInfos.Push(newInfo);
            }

            Sort(methodDeclarations.Begin(), methodDeclarations.End(), ComparePropertyStrings);
            Sort(propertyInfos.Begin(), propertyInfos.End(), ComparePropertyInfos);

            if (!methodDeclarations.Empty())
            {
                if (mode == DOXYGEN)
                    Log::WriteRaw("\nMethods:\n\n");
                else if (mode == C_HEADER)
                    Log::WriteRaw("// Methods:\n");
                for (const String& methodDeclaration : methodDeclarations)
                    OutputAPIRow(mode, methodDeclaration);
            }

            if (!propertyInfos.Empty())
            {
                if (mode == DOXYGEN)
                    Log::WriteRaw("\nProperties:\n\n");
                else if (mode == C_HEADER)
                    Log::WriteRaw("\n// Properties:\n");
                for (const PropertyInfo& propertyInfo : propertyInfos)
                {
                    String remark;
                    String cppdoc;
                    if (!propertyInfo.write_)
                        remark = "readonly";
                    else if (!propertyInfo.read_)
                        remark = "writeonly";
                    if (!remark.Empty())
                    {
                        if (mode == DOXYGEN)
                        {
                            remark = " // " + remark;
                        }
                        else if (mode == C_HEADER)
                        {
                            cppdoc = "/* " + remark + " */\n";
                            remark.Clear();
                        }
                    }

                    OutputAPIRow(mode, cppdoc + propertyInfo.type_ + " " + propertyInfo.name_ + remark);
                }
            }

            // Check for namespaced constants to be included in the class documentation
            HashMap<String, Vector<String>>::ConstIterator gcIt = globalConstants.Find(typeName);
            if (gcIt != globalConstants.End())
            {
                String prefix;
                if (mode == DOXYGEN)
                {
                    Log::WriteRaw("\nConstants:\n\n");
                }
                else if (mode == C_HEADER)
                {
                    Log::WriteRaw("\n// Constants:\n");
                    prefix = "static const ";
                }

                const Vector<String>& constants = gcIt->second_;
                for (const String& constant : constants)
                    OutputAPIRow(mode, prefix + constant);
            }


            if (mode == DOXYGEN)
                Log::WriteRaw("\n");
            else if (mode == C_HEADER)
                Log::WriteRaw("};\n");
        }
    }

    Vector<PropertyInfo> globalPropertyInfos;
    Vector<String> globalFunctions;

    unsigned functions = scriptEngine_->GetGlobalFunctionCount();
    for (unsigned i = 0; i < functions; ++i)
    {
        asIScriptFunction* function = scriptEngine_->GetGlobalFunctionByIndex(i);
        String functionName(function->GetName());
        String declaration(function->GetDeclaration(true, false, true));

        // Recreate tab escape sequences
        declaration.Replace("\t", "\\t");

        if (functionName.Contains("set_") || functionName.Contains("get_"))
            ExtractPropertyInfo(functionName, declaration, globalPropertyInfos);
        else
            globalFunctions.Push(declaration);
    }

    Sort(globalFunctions.Begin(), globalFunctions.End(), ComparePropertyStrings);
    Sort(globalPropertyInfos.Begin(), globalPropertyInfos.End(), ComparePropertyInfos);

    if (mode == DOXYGEN)
        Log::WriteRaw("\\section ScriptAPI_Enums Enumerations\n");
    else if (mode == C_HEADER)
        Log::WriteRaw("\n// Enumerations\n");

    unsigned enums = scriptEngine_->GetEnumCount();
    Vector<Pair<String, unsigned>> sortedEnums;
    for (unsigned i = 0; i < enums; ++i)
        sortedEnums.Push(MakePair(String(scriptEngine_->GetEnumByIndex(i)->GetName()), i));
    Sort(sortedEnums.Begin(), sortedEnums.End());

    for (unsigned i = 0; i < sortedEnums.Size(); ++i)
    {
        asITypeInfo* enumType = scriptEngine_->GetEnumByIndex(sortedEnums[i].second_);
        int typeId = enumType->GetTypeId();

        if (mode == DOXYGEN)
            Log::WriteRaw("\n### " + String(enumType->GetName()) + "\n\n");
        else if (mode == C_HEADER)
            Log::WriteRaw("\nenum " + String(enumType->GetName()) + "\n{\n");

        for (unsigned j = 0; j < enumType->GetEnumValueCount(); ++j)
        {
            int value = 0;
            const char* name = enumType->GetEnumValueByIndex(j, &value);
            OutputAPIRow(mode, String(name), false, ",");
        }

        if (mode == DOXYGEN)
            Log::WriteRaw("\n");
        else if (mode == C_HEADER)
            Log::WriteRaw("};\n");
    }

    if (mode == DOXYGEN)
        Log::WriteRaw("\\section ScriptAPI_GlobalFunctions Global functions\n");
    else if (mode == C_HEADER)
        Log::WriteRaw("\n// Global functions\n");

    for (const String& globalFunction : globalFunctions)
        OutputAPIRow(mode, globalFunction);

    if (mode == DOXYGEN)
        Log::WriteRaw("\\section ScriptAPI_GlobalProperties Global properties\n");
    else if (mode == C_HEADER)
        Log::WriteRaw("\n// Global properties\n");

    for (const PropertyInfo& globalPropertyInfo : globalPropertyInfos)
        OutputAPIRow(mode, globalPropertyInfo.type_ + " " + globalPropertyInfo.name_, true);

    if (mode == DOXYGEN)
        Log::WriteRaw("\\section ScriptAPI_GlobalConstants Global constants\n");
    else if (mode == C_HEADER)
        Log::WriteRaw("\n// Global constants\n");

    const Vector<String>& noNameSpaceConstants = globalConstants[String()];
    for (const String& noNameSpaceConstant : noNameSpaceConstants)
        OutputAPIRow(mode, noNameSpaceConstant, true);

    if (mode == DOXYGEN)
        Log::WriteRaw("*/\n\n}\n");
}

}
