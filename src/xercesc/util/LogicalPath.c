#if !defined(WEAVEPATH_CPP)
#define WEAVEPATH_CPP

/***
 *
 *  Previously, each <OS>PlatformUtils.cpp has its onw copy of the
 *  method weavePaths(), and almost of them implemented the same logic,
 *  with few platform specific difference, and unfortunately that 
 *  implementation was wrong.
 *  
 *  The only platform specific issue is slash character.
 *  On all platforms other than Windows, chForwardSlash and chBackSlash 
 *  are considered slash, while on Windows, two additional characters, 
 *  chYenSign and chWonSign are slash as well.
 *
 *  The idea is to maintain a SINGLE copy of this method rather than
 *  each <OS>PlatformUtils.cpp has its own copy, we introduce a new
 *  method, XMLPlatformUtils::isAnySlash(), to replace the direct checking
 *  code ( if ( c == chForwardSlash || c == chBackSlash).
 *
 *  With this approach, we might have a performance hit since isAnySlash() 
 *  is so frequently used in this implementation, so we intend to make it 
 *  inline. Then we face a complier issue.
 *
 *  There are two compilation units involved, one is PlatformUtils.cpp and 
 *  the other <OS>PlatformUtils.cpp. When PlatformUtils.cp get compiled,
 *  the weavePath(), remove**Slash() have dependency upon isAnySlash() which
 *  is in <OS>PlatformUtils.cpp (and what is worse, it is inlined), so we have
 *  undefined/unresolved symbol: isAnySlash() on AIX/xlc_r, Solaris/cc and 
 *  Linux/gcc, while MSVC and HP/aCC are fine with this.
 *  
 *  That means we can not place these new methods in PlatformUtils.cpp with
 *  inlined XMLPlatformUtils::isAnySlash() in <OS>PlatformUtils.cpp.
 *
 *  The solution to this is <os>PlatformUtils.cpp will include this file so that
 *  we have only one copy of these methods while get compiled in <os>PlatformUtils
 *  inlined isAnySlash().
 *
 ***/
XMLCh* XMLPlatformUtils::weavePaths(const XMLCh* const    basePath
                                  , const XMLCh* const    relativePath)

{
    // Create a buffer as large as both parts and empty it
    XMLCh* tmpBuf = new XMLCh[XMLString::stringLen(basePath) + 
                              XMLString::stringLen(relativePath) + 2];
    *tmpBuf = 0;

    //
    //  If we have no base path, then just take the relative path as is.
    //
    if ((!basePath) || (!*basePath))
    {
        XMLString::copyString(tmpBuf, relativePath);
        return tmpBuf;
    }

    //
    // Remove anything after the last slash
    //
    const XMLCh* basePtr = basePath + (XMLString::stringLen(basePath) - 1);
    while ((basePtr >= basePath)  &&  ((isAnySlash(*basePtr) == false)))
    {
        basePtr--;
    }

    // There is no relevant base path, so just take the relative part
    if (basePtr < basePath)
    {
        XMLString::copyString(tmpBuf, relativePath);
        return tmpBuf;
    }

    //
    // 1. concatenate the base and relative
    // 2. remove all occurences of "/./"
    // 3. remove all occurences of segment/../ where segment is not ../
	// 

    XMLString::subString(tmpBuf, basePath, 0, (basePtr - basePath + 1));
    tmpBuf[basePtr - basePath + 1] = 0;
    XMLString::catString(tmpBuf, relativePath);

    removeDotSlash(tmpBuf);

    removeDotDotSlash(tmpBuf);

    return tmpBuf;

}

//
// Remove all occurences of './' when it is part of '/./'
//
// Since it could be '.\' or other combination on windows ( eg, '.'+chYanSign)
// we can't make use of patterMatch().
//
//
void XMLPlatformUtils::removeDotSlash(XMLCh* const path)
{
    if ((!path) || (!*path))
        return;

    XMLCh* srcPtr = XMLString::replicate(path);
    int    srcLen = XMLString::stringLen(srcPtr);
    ArrayJanitor<XMLCh>   janName(srcPtr);   
    XMLCh* tarPtr = path;

    while (*srcPtr)
    {
        if ( 3 <= srcLen )
        {
            if ( (isAnySlash(*srcPtr))     &&
                (chPeriod == *(srcPtr+1)) &&
                (isAnySlash(*(srcPtr+2)))  )
            {
                // "\.\x" seen
                // skip the first two, and start from the 3rd,
                // since "\x" could be another "\."
                srcPtr+=2;              
                srcLen-=2;
            }
            else
            {
                *tarPtr++ = *srcPtr++;  // eat the current char
                srcLen--;
            }
        }
        else if ( 1 == srcLen )
        {
            *tarPtr++ = *srcPtr++;
        }
        else if ( 2 == srcLen)
        {
            *tarPtr++ = *srcPtr++;
            *tarPtr++ = *srcPtr++;
        }

    }

    *tarPtr = 0;

    return;
}

//
// Remove all occurences of '/segment/../' when segment is not '..'
//
// Cases with extra /../ is left to the underlying file system.
//
void XMLPlatformUtils::removeDotDotSlash(XMLCh* const path)
{
    int pathLen = XMLString::stringLen(path);
    XMLCh* tmp1 = new XMLCh [pathLen+1];
    ArrayJanitor<XMLCh>   tmp1Name(tmp1);

    XMLCh* tmp2 = new XMLCh [pathLen+1];
    ArrayJanitor<XMLCh>   tmp2Name(tmp2);

    // remove all "<segment>/../" where "<segment>" is a complete
    // path segment not equal to ".."
    int index = -1;
    int segIndex = -1;
    int offset = 1;

    while ((index = searchSlashDotDotSlash(&(path[offset]))) != -1)
    {
        // Undo offset
        index += offset;

        // Find start of <segment> within substring ending at found point.
        XMLString::subString(tmp1, path, 0, index-1);
        segIndex = index - 1;
        while ((segIndex >= 0) && (!isAnySlash(tmp1[segIndex])))
        {
            segIndex--;
        }

        // Ensure <segment> exists and != ".."
        if (segIndex >= 0                 &&
            (path[segIndex+1] != chPeriod ||
             path[segIndex+2] != chPeriod ||
             segIndex + 3 != index))
        {

            XMLString::subString(tmp1, path, 0, segIndex);
            XMLString::subString(tmp2, path, index+3, XMLString::stringLen(path));

            path[0] = 0;
            XMLString::catString(path, tmp1);
            XMLString::catString(path, tmp2);

            offset = (segIndex == 0 ? 1 : segIndex);
        }
        else
        {
            offset += 4;
        }

    }// while

}

int XMLPlatformUtils::searchSlashDotDotSlash(XMLCh* const srcPath)
{
    if ((!srcPath) || (!*srcPath))
        return -1;

    XMLCh* srcPtr = srcPath;
    int    srcLen = XMLString::stringLen(srcPath);
    int    retVal = -1;

    while (*srcPtr)
    {
        if ( 4 <= srcLen )
        {
            if ( (isAnySlash(*srcPtr))     &&
                 (chPeriod == *(srcPtr+1)) &&
                 (chPeriod == *(srcPtr+2)) &&
                 (isAnySlash(*(srcPtr+3)))  )
            {
                retVal = (srcPtr - srcPath);
                break;
            }
            else
            {
                srcPtr++;
                srcLen--;
            }
        }
        else 
        {
            break;
        }

    } // while

    return retVal;

}

#endif
