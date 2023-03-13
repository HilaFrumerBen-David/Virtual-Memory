//
// Created by omer_siton on 19/05/2022.
//
#include "VirtualMemory.h"
#include "PhysicalMemory.h"


/**
 * create an offset array based on the virtual address and the Memory Constants
 * @param virtualAddress
 * @param arr - return this arr
 * @param size - the size of the array
 */
void split_virtual_address (uint64_t virtualAddress, uint64_t * arr, word_t size)
{
    uint64_t mask = PAGE_SIZE -1;
    for (int i=0 ; i < size; i++)
    {
        arr[i] =((virtualAddress & mask) >> (OFFSET_WIDTH * i));
        mask = mask << OFFSET_WIDTH;
    }
}
/**
 * traverse the tree and return the updated parameters we sent to the function so we could use this information in findFrame function
 * @param curFrame - current frame of the tree we're looking at
 * @param parentFrame - parent frame of the tree we're looking at
 * @param curDepth - current depth of current frame we're looking at
 * @param maxFrame - max frame variable we return
 * @param maxDistParentFrame - max cyclic dist parent frame we return
 * @param maxDist - max dist tmp variable to find the max dist frame
 * @param pageSwappedIn - the page number we want to read / write
 * @param pagePath - saves the path for the current frame
 * @param maxDistPagePath - max cyclic dist page path we return
 * @param maxDistFrame - max cyclic dist frame we return
 * @param emptyFrame - empty frame number we return
 * @param frameEvict - the frame number we can't evict
 * @param emptyFrameOffset - empty frame offset in parent frame
 * @param emptyFrameParent - empty frame parent
 */
void DFS(word_t curFrame, word_t parentFrame, word_t curDepth, word_t* maxFrame,
         word_t * maxDistParentFrame, uint64_t * maxDist, uint64_t pageSwappedIn,
         uint64_t pagePath, uint64_t * maxDistPagePath, word_t *maxDistFrame, word_t* emptyFrame,
         word_t frameEvict, uint64_t * emptyFrameOffset, word_t* emptyFrameParent){

    // check if we found an empty frame - if yes we don't need to keep the traversing
    if (*emptyFrame)
        return;
    // max frame variable update
    (curFrame >= *maxFrame) ? *maxFrame = curFrame : *maxFrame;
    // if we arrive to the page level
    // we'll calculate the cyclical dist by this notion:
    // min{NUM_PAGES - |page_swapped_in - p|, |page_swapped_in - p|}
    if (curDepth == TABLES_DEPTH){
        uint64_t resDist = pageSwappedIn > pagePath ? pageSwappedIn - pagePath : pagePath - pageSwappedIn;
        uint64_t tmpMinDist = NUM_PAGES - resDist > resDist ? resDist : NUM_PAGES - resDist;
        if(tmpMinDist > *maxDist){
            *maxDist = tmpMinDist;
            *maxDistFrame = curFrame; // frame to evict
            *maxDistPagePath = pagePath;
            *maxDistParentFrame = parentFrame;
        }
        return;
    }
    // for each frame go over all rows and check for empty frame / go deeper in recursion
    word_t counterZero = 0;
    for (word_t i = 0; i < PAGE_SIZE; ++i){
        word_t tmpWord; //the value in row i in curFrame
        word_t tmpAddr = curFrame * PAGE_SIZE + i;
        PMread (tmpAddr, &tmpWord);
        if (tmpWord)
            DFS (tmpWord, curFrame, curDepth + 1, maxFrame, maxDistParentFrame, maxDist,
                 pageSwappedIn, (pagePath | (i << ((TABLES_DEPTH - curDepth - 1) * OFFSET_WIDTH))) ,
                 maxDistPagePath, maxDistFrame, emptyFrame, frameEvict, emptyFrameOffset, emptyFrameParent);
        else
            counterZero ++;
    }
    // if a frame is empty and doesn't equal to frame evict we'll return it as empty frame and finish the recursion
    if (counterZero == PAGE_SIZE && (frameEvict != curFrame)){
        *emptyFrame = curFrame;
        *emptyFrameOffset = ((pagePath >> ((TABLES_DEPTH - curDepth) * OFFSET_WIDTH)) & (PAGE_SIZE - 1));
        *emptyFrameParent = parentFrame;
        return;
    }
}
/**
 * fill with zeros the input frame
 * @param frame
 */
void fillWithZeros(word_t frame){
    for (int i = 0; i < PAGE_SIZE; ++i)
        PMwrite(frame * PAGE_SIZE + i, 0);
}
/**
 * classify the option that was returned from the DFS
 * find a frame based on the 1 of the 3 options for an empty frame
 * @param virtualAddress
 * @param frameEvict - the frame we can't return
 * @return the empty frame
 */
word_t findFrame(uint64_t virtualAddress, word_t frameEvict)
{
    // init variables
    word_t maxFrame = 0, maxDistFrame = 0, curDepth = 0, emptyFrame = 0;
    word_t parentFrame = 0, pagePath = 0, maxDistParentFrame = 0, emptyFrameParent = 0;
    uint64_t maxDistPagePath = 0, emptyFrameOffset = 0, maxDist = 0;
    word_t pageSwappedIn = virtualAddress >> OFFSET_WIDTH;

    DFS(0, parentFrame, curDepth, &maxFrame, &maxDistParentFrame,
        &maxDist, pageSwappedIn, pagePath, (&maxDistPagePath), &maxDistFrame,
        &emptyFrame, frameEvict, &emptyFrameOffset, &emptyFrameParent);

    // case 1 - A frame containing an empty table
    if (emptyFrame)
    {
        // unlink maxDistFrame from his parent
        PMwrite((emptyFrameParent) * PAGE_SIZE + emptyFrameOffset, 0);
        return emptyFrame;
    }
    // case 2 - An unused frame
    if (maxFrame + 1 < NUM_FRAMES)
    {
        // we found an unused frame
        return (maxFrame + 1);
    }
    // case 3 - all frames are already used, so we need to evict a page from some frame
    if (maxDistFrame)
    {
        // evict the page data to disk
        PMevict(maxDistFrame, maxDistPagePath);
        // calculate offset
        word_t offset = maxDistPagePath & (PAGE_SIZE - 1);
        // unlink maxDistFrame from his parent
        PMwrite(maxDistParentFrame * PAGE_SIZE + offset, 0);
        return maxDistFrame;
    }
    return -1;
}

/**
 * translate the virtual address to a physical address
 * @param virtualAddress
 * @return the leaf frame that holds the page data
 */
word_t translate(uint64_t virtualAddress)
{
    word_t size_arr = TABLES_DEPTH + 1;
    uint64_t offsets_arr[size_arr];
    split_virtual_address(virtualAddress, offsets_arr, size_arr);
    word_t tmpAddress = 0, frameEvict;
    uint64_t curWord;

    for(int i = size_arr - 1; i > 0; --i){    // i > 1 : table level, i = : leaf level
        frameEvict = tmpAddress;
        curWord = (tmpAddress * PAGE_SIZE) + offsets_arr[i];
        PMread(curWord, &tmpAddress);
        if (tmpAddress == 0)
        {
            // find unused frame or evict a page from a frame
            word_t newFrame = findFrame (virtualAddress, frameEvict);
            // zero the new frame if it's a new table frame
            if (i > 1)
                fillWithZeros(newFrame);
            // link to parent
            PMwrite(curWord, newFrame);
            // restore if we are in the leaf level
            if (i == 1)
                PMrestore(newFrame, (virtualAddress >> OFFSET_WIDTH));
            // update tmpAddress
            tmpAddress = newFrame;
        }
    }
    return tmpAddress;
}

/***************************************************************
 * Class API
 ***************************************************************/
/*
 * Initialize the virtual memory.
 */
void VMinitialize()
{
    // go to the first frame, put 0 in each row
    fillWithZeros(0);
}

/* Reads a word from the given virtual address
 * and puts its content in *value.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
 */
int VMread(uint64_t virtualAddress, word_t* value)
{
    // check for a valid input
    if (virtualAddress < 0 || virtualAddress >= VIRTUAL_MEMORY_SIZE)
        return 0;
    // translate the first address
    uint64_t mask = PAGE_SIZE - 1;
    uint64_t offset = virtualAddress & mask;
    // translate a virtual address to frame
    word_t frame = translate (virtualAddress);
    // get the physical address
    uint64_t physicalAddress = frame * PAGE_SIZE + offset;
    // read the value in this frame
    PMread (physicalAddress, value);
    return 1;
}

/* Writes a word to the given virtual address.
 *
 * returns 1 on success.
 * returns 0 on failure (if the address cannot be mapped to a physical
 * address for any reason)
*/
int VMwrite(uint64_t virtualAddress, word_t value)
{
    // check for a valid input
    if (virtualAddress < 0 || virtualAddress >= VIRTUAL_MEMORY_SIZE)
        return 0;
    // translate the first address
    uint64_t mask = PAGE_SIZE -1;
    uint64_t offset = virtualAddress & mask;
    // translate a virtual address to frame
    word_t frame = translate (virtualAddress);
    // get the physical address
    uint64_t physicalAddress = frame * PAGE_SIZE + offset;
    // read the value in this frame
    PMwrite(physicalAddress, value);
    return 1;
}
