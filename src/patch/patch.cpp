//  patch.cpp
//  ZipPatch
/*
 The MIT License (MIT)
 Copyright (c) 2016-2018 HouSisong
 
 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:
 
 The above copyright notice and this permission notice shall be
 included in all copies of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
 */
#include "patch.h"
#include "../../HDiffPatch/libHDiffPatch/HPatch/patch.h" //https://github.com/sisong/HDiffPatch
#include "../../HDiffPatch/file_for_patch.h"
#include "patch_types.h"
#include "Zipper.h"
#include "ZipDiffData.h"
#include "OldStream.h"
#include "NewStream.h"

#define  check(value,error) { \
    if (!(value)){ printf(#value" "#error"!\n");  \
        if (result!=PATCH_SUCCESS) result=error; if (!_isInClear){ goto clear; } } }

TPatchResult ZipPatch(const char* oldZipPath,const char* zipDiffPath,const char* outNewZipPath,
                      size_t maxUncompressMemory,const char* tempUncompressFileName){
    #define CACHE_SIZE  (1<<20)
    UnZipper            oldZip;
    TFileStreamInput    diffData;
    Zipper              out_newZip;
    ZipDiffData         zipDiffData;
    OldStream           oldStream;
    NewStream           newStream;
    hpatch_compressedDiffInfo   diffInfo;
    TByte*                      ref_cache=0;
    TFileStreamInput            input_refFile;
    TFileStreamOutput           output_refFile;
    hpatch_TStreamInput         input_ref_cache;
    hpatch_TStreamOutput        output_ref_cache;
    hpatch_TStreamInput*        input_ref=0;
    hpatch_TStreamOutput*       output_ref=0;
    TPatchResult    result=PATCH_SUCCESS;
    bool            _isInClear=false;
    bool            isUsedTempFile=false;
    TByte*          temp_cache =0;
    ZipFilePos_t    decompressSumSize=0;
    hpatch_TDecompress* decompressPlugin=0;

    UnZipper_init(&oldZip);
    TFileStreamInput_init(&diffData);
    Zipper_init(&out_newZip);
    ZipDiffData_init(&zipDiffData);
    OldStream_init(&oldStream);
    NewStream_init(&newStream);
    TFileStreamInput_init(&input_refFile);
    TFileStreamOutput_init(&output_refFile);
    
    check(TFileStreamInput_open(&diffData,zipDiffPath),PATCH_READ_ERROR);
#ifdef _CompressPlugin_zstd
    if (decompressPlugin==0){
        if (ZipDiffData_isCanDecompress(&diffData,&zstdDecompressPlugin))
            decompressPlugin=&zstdDecompressPlugin;
    }
#endif
    if (decompressPlugin==0){
        decompressPlugin=&zlibDecompressPlugin;
        check(ZipDiffData_isCanDecompress(&diffData,decompressPlugin),PATCH_COMPRESSTYPE_ERROR);
    }
    
    check(ZipDiffData_openRead(&zipDiffData,&diffData,decompressPlugin),PATCH_ZIPDIFFINFO_ERROR);
    check(UnZipper_openRead(&oldZip,oldZipPath,zipDiffData.oldZipIsNormalized!=0),PATCH_READ_ERROR);
    check(zipDiffData.oldZipVCESize==oldZip._vce_size,PATCH_OLDDATA_ERROR);
    check(zipDiffData.oldCrc==OldStream_getOldCrc(&oldZip,zipDiffData.oldRefList,
                                                  zipDiffData.oldRefCount), PATCH_OLDDATA_ERROR);
    
    check(getCompressedDiffInfo(&diffInfo,zipDiffData.hdiffzData),PATCH_HDIFFINFO_ERROR);
    if (strlen(diffInfo.compressType) > 0)
        check(decompressPlugin->is_can_open(decompressPlugin,&diffInfo),PATCH_COMPRESSTYPE_ERROR);
    
    decompressSumSize=OldStream_getDecompressSumSize(&oldZip,zipDiffData.oldRefList,zipDiffData.oldRefCount);
    
    isUsedTempFile=(decompressSumSize > maxUncompressMemory)&&(tempUncompressFileName!=0);
    if (isUsedTempFile){
        check(TFileStreamOutput_open(&output_refFile,tempUncompressFileName,decompressSumSize),PATCH_READ_ERROR);
        check(TFileStreamInput_open(&input_refFile,tempUncompressFileName),PATCH_READ_ERROR);
        input_refFile.base.streamSize=decompressSumSize;
        output_ref=&output_refFile.base;
        input_ref=&input_refFile.base;
    }else{
        ref_cache=(TByte*)malloc(decompressSumSize+1);//+1 not null
        check(ref_cache!=0, PATCH_MEM_ERROR);
        mem_as_hStreamOutput(&output_ref_cache,ref_cache,ref_cache+decompressSumSize);
        mem_as_hStreamInput(&input_ref_cache,ref_cache,ref_cache+decompressSumSize);
        output_ref=&output_ref_cache;
        input_ref=&input_ref_cache;
    }
    
    check(OldStream_getDecompressData(&oldZip,zipDiffData.oldRefList,
                                      zipDiffData.oldRefCount,output_ref), PATCH_OLDDATA_ERROR);
    check(TFileStreamOutput_close(&output_refFile),PATCH_CLOSEFILE_ERROR);
    check(OldStream_open(&oldStream,&oldZip,zipDiffData.oldRefList,
                         zipDiffData.oldRefCount,input_ref), PATCH_OLDDATA_ERROR);
    check(oldStream.stream->streamSize==diffInfo.oldDataSize,PATCH_OLDDATA_ERROR);

    check(Zipper_openWrite(&out_newZip,outNewZipPath,(int)zipDiffData.newZipFileCount,
                           (int)zipDiffData.newZipAlignSize), PATCH_READ_ERROR)
    check(NewStream_open(&newStream,&out_newZip,&oldZip,  (size_t)diffInfo.newDataSize,
                         zipDiffData.newZipIsNormalized!=0,zipDiffData.newZipVCESize,
                         zipDiffData.samePairList,zipDiffData.samePairCount,
                         zipDiffData.newReCompressedSizeList,zipDiffData.newReCompressedSizeCount),PATCH_NEWDATA_ERROR);
    
    temp_cache =(TByte*)malloc(CACHE_SIZE);
    check(temp_cache!=0,PATCH_MEM_ERROR);
    check(patch_decompress_with_cache(newStream.stream,oldStream.stream,zipDiffData.hdiffzData,
                                      decompressPlugin,temp_cache,temp_cache+CACHE_SIZE),PATCH_ERROR);
    check(newStream.isFilish,PATCH_NEWDATA_ERROR);
    
clear:
    _isInClear=true;
    check(Zipper_close(&out_newZip),PATCH_CLOSEFILE_ERROR);
    NewStream_close(&newStream);
    OldStream_close(&oldStream);
    check(UnZipper_close(&oldZip),PATCH_CLOSEFILE_ERROR);
    ZipDiffData_close(&zipDiffData);
    check(TFileStreamInput_close(&diffData),PATCH_CLOSEFILE_ERROR);
    check(TFileStreamOutput_close(&output_refFile),PATCH_CLOSEFILE_ERROR);
    check(TFileStreamInput_close(&input_refFile),PATCH_CLOSEFILE_ERROR);
    if (isUsedTempFile) remove(tempUncompressFileName);
    if (temp_cache) free(temp_cache);
    if (ref_cache) free(ref_cache);
    return result;
}
