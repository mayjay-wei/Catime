/**
 * @file font_ttf_parser.c
 * @brief TTF/OTF binary parsing implementation
 */

#include "font/font_ttf_parser.h"
#include "utils/string_convert.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * TTF Binary Structures (Big-Endian)
 * ============================================================================
 */

#pragma pack(push, 1)

typedef struct {
    WORD platformID;
    WORD encodingID;
    WORD languageID;
    WORD nameID;
    WORD length;
    WORD offset;
} NameRecord;

typedef struct {
    WORD format;
    WORD count;
    WORD stringOffset;
} NameTableHeader;

typedef struct {
    DWORD tag;
    DWORD checksum;
    DWORD offset;
    DWORD length;
} TableRecord;

typedef struct {
    DWORD sfntVersion;
    WORD numTables;
    WORD searchRange;
    WORD entrySelector;
    WORD rangeShift;
} FontDirectoryHeader;

#pragma pack(pop)

/* ============================================================================
 * Byte Order Conversion (Big-Endian ↔ Little-Endian)
 * ============================================================================
 */

static inline WORD SwapWORD(WORD value) {
    return ((value & 0xFF) << 8) | ((value & 0xFF00) >> 8);
}

static inline DWORD SwapDWORD(DWORD value) {
    return ((value & 0xFF) << 24) | ((value & 0xFF00) << 8) |
           ((value & 0xFF0000) >> 8) | ((value & 0xFF00'0000) >> 24);
}

/* ============================================================================
 * TTF Table Lookup
 * ============================================================================
 */

/**
 * @brief Find specific table in TTF directory
 * @param hFile Open file handle
 * @param numTables Number of tables in directory
 * @param targetTag Table tag to find
 * @param outOffset Output: table offset
 * @param outLength Output: table length
 * @return true if table found
 */
static bool FindTTFTable(HANDLE hFile, WORD numTables, DWORD targetTag,
                         DWORD* outOffset, DWORD* outLength) {
    if (hFile == INVALID_HANDLE_VALUE || !outOffset || !outLength)
        return false;

    for (WORD i = 0; i < numTables; i++) {
        TableRecord tableRecord;
        DWORD bytesRead;

        if (!ReadFile(hFile, &tableRecord, sizeof(TableRecord), &bytesRead,
                      NULL) ||
            bytesRead != sizeof(TableRecord)) {
            return false;
        }

        if (tableRecord.tag == targetTag) {
            *outOffset = SwapDWORD(tableRecord.offset);
            *outLength = SwapDWORD(tableRecord.length);
            return true;
        }
    }

    return false;
}

/* ============================================================================
 * String Parsing
 * ============================================================================
 */

/**
 * @brief Parse font name from TTF name table entry
 * @param stringData Raw string data
 * @param dataLength Data length
 * @param isUnicode true for UTF-16BE, false for ASCII
 * @param outName Output buffer
 * @param outNameSize Buffer size
 */
static void ParseFontName(const char* stringData, size_t dataLength,
                          bool isUnicode, char* outName, size_t outNameSize) {
    if (!stringData || !outName || outNameSize == 0)
        return;

    if (isUnicode) {
        /* UTF-16 Big-Endian → UTF-16 Little-Endian → UTF-8 */
        WCHAR* unicodeStr = (WCHAR*)stringData;
        int numChars = (int)(dataLength / 2);

        /* Swap byte order */
        for (int i = 0; i < numChars; i++) {
            unicodeStr[i] = SwapWORD(unicodeStr[i]);
        }
        unicodeStr[numChars] = 0;

        /* Convert to UTF-8 */
        WideToUtf8(unicodeStr, outName, outNameSize);
    } else {
        /* ASCII → UTF-8 (direct copy) */
        size_t copyLen =
            (dataLength < outNameSize - 1) ? dataLength : outNameSize - 1;
        memcpy(outName, stringData, copyLen);
        outName[copyLen] = '\0';
    }
}

/* ============================================================================
 * Name Table Parsing
 * ============================================================================
 */

/**
 * @brief Extract font family name from open TTF file
 * @param hFile Open file handle (positioned at start)
 * @param fontName Output buffer
 * @param fontNameSize Buffer size
 * @return true on success
 */
static bool ExtractFontNameFromHandle(HANDLE hFile, char* fontName,
                                      size_t fontNameSize) {
    if (hFile == INVALID_HANDLE_VALUE || !fontName || fontNameSize == 0)
        return false;

    /* Read font directory header */
    FontDirectoryHeader fontHeader;
    DWORD bytesRead;

    if (!ReadFile(hFile, &fontHeader, sizeof(FontDirectoryHeader), &bytesRead,
                  NULL) ||
        bytesRead != sizeof(FontDirectoryHeader)) {
        return false;
    }

    fontHeader.numTables = SwapWORD(fontHeader.numTables);

    /* Locate 'name' table */
    DWORD nameTableOffset = 0, nameTableLength = 0;
    if (!FindTTFTable(hFile, fontHeader.numTables, TTF_NAME_TABLE_TAG,
                      &nameTableOffset, &nameTableLength)) {
        return false;
    }

    /* Seek to name table */
    if (SetFilePointer(hFile, nameTableOffset, NULL, FILE_BEGIN) ==
        INVALID_SET_FILE_POINTER) {
        return false;
    }

    /* Read name table header */
    NameTableHeader nameHeader;
    if (!ReadFile(hFile, &nameHeader, sizeof(NameTableHeader), &bytesRead,
                  NULL) ||
        bytesRead != sizeof(NameTableHeader)) {
        return false;
    }

    nameHeader.count = SwapWORD(nameHeader.count);
    nameHeader.stringOffset = SwapWORD(nameHeader.stringOffset);

    /* Search for font family name (ID=1) */
    bool foundName = false;
    WORD nameLength = 0, nameOffset = 0;
    bool isUnicode = false;

    for (WORD i = 0; i < nameHeader.count; i++) {
        NameRecord nameRecord;

        if (!ReadFile(hFile, &nameRecord, sizeof(NameRecord), &bytesRead,
                      nullptr) ||
            bytesRead != sizeof(NameRecord)) {
            return false;
        }

        /* Convert from big-endian */
        nameRecord.platformID = SwapWORD(nameRecord.platformID);
        nameRecord.encodingID = SwapWORD(nameRecord.encodingID);
        nameRecord.nameID = SwapWORD(nameRecord.nameID);
        nameRecord.length = SwapWORD(nameRecord.length);
        nameRecord.offset = SwapWORD(nameRecord.offset);

        /* Look for family name (ID=1) */
        if (nameRecord.nameID != TTF_NAME_ID_FAMILY) {
            continue;
        }

        /* Prefer Windows Unicode (platform 3, encoding 1) */
        if (nameRecord.platformID == 3 && nameRecord.encodingID == 1) {
            nameLength = nameRecord.length;
            nameOffset = nameRecord.offset;
            isUnicode = true;
            foundName = true;
            break;
        }
        if (!foundName) {
            /* Fallback to first family name found */
            nameLength = nameRecord.length;
            nameOffset = nameRecord.offset;
            isUnicode = (nameRecord.platformID == 0);
            foundName = true;
        }
    }

    if (!foundName)
        return false;

    /* Seek to string data */
    DWORD stringDataOffset = nameTableOffset + sizeof(NameTableHeader) +
                             nameHeader.count * sizeof(NameRecord) + nameOffset;

    if (SetFilePointer(hFile, stringDataOffset, NULL, FILE_BEGIN) ==
        INVALID_SET_FILE_POINTER) {
        return false;
    }

    /* Read string data */
    if (nameLength > TTF_STRING_SAFETY_LIMIT) {
        nameLength = TTF_STRING_SAFETY_LIMIT;
    }

    char* stringBuffer = (char*)malloc(nameLength + 2);
    if (!stringBuffer)
        return false;

    bool success = false;
    if (ReadFile(hFile, stringBuffer, nameLength, &bytesRead, NULL) &&
        bytesRead == nameLength) {
        ParseFontName(stringBuffer, nameLength, isUnicode, fontName,
                      fontNameSize);
        success = true;
    }

    free(stringBuffer);
    return success;
}

/* ============================================================================
 * Public API Implementation
 * ============================================================================
 */

bool GetFontNameFromFile(const char* fontFilePath, char* fontName,
                         size_t fontNameSize) {
    if (!fontFilePath || !fontName || fontNameSize == 0)
        return false;

    /* Convert path to wide */
    wchar_t wFontPath[MAX_PATH];
    if (!Utf8ToWide(fontFilePath, wFontPath, MAX_PATH))
        return false;

    /* Open font file */
    HANDLE hFile = CreateFileW(wFontPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    /* Parse and extract name */
    bool result = ExtractFontNameFromHandle(hFile, fontName, fontNameSize);

    CloseHandle(hFile);
    return result;
}
