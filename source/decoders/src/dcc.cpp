//
// Created by Lectem.
//

#include "dcc.h"
#include <BitStream.h>
#include <assert.h>
#include "palette.h"
#include "utils.h"

namespace WorldStone
{

// constexpr unsigned DCC::bitsWidthTable[16] = {0,  1,  2,  4,  6,  8,  10, 12,
//                                              14, 16, 20, 24, 26, 28, 30, 32};

using readUnsignedPtrType = uint32_t (WorldStone::BitStream::*)(void);
using readSignedPtrType   = int32_t (WorldStone::BitStream::*)(void);

static constexpr readUnsignedPtrType readUnsignedPtrs[16] = {
    &BitStream::read0Bits,
    &BitStream::readBit,
    &BitStream::readUnsigned<DCC::bitsWidthTable[2]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[3]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[4]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[5]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[6]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[7]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[8]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[9]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[10]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[11]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[12]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[13]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[14]>,
    &BitStream::readUnsigned<DCC::bitsWidthTable[15]>};

static constexpr readSignedPtrType readSignedPtrs[16] = {
    &BitStream::readSigned<DCC::bitsWidthTable[0]>,
    &BitStream::readSigned<DCC::bitsWidthTable[1]>,
    &BitStream::readSigned<DCC::bitsWidthTable[2]>,
    &BitStream::readSigned<DCC::bitsWidthTable[3]>,
    &BitStream::readSigned<DCC::bitsWidthTable[4]>,
    &BitStream::readSigned<DCC::bitsWidthTable[5]>,
    &BitStream::readSigned<DCC::bitsWidthTable[6]>,
    &BitStream::readSigned<DCC::bitsWidthTable[7]>,
    &BitStream::readSigned<DCC::bitsWidthTable[8]>,
    &BitStream::readSigned<DCC::bitsWidthTable[9]>,
    &BitStream::readSigned<DCC::bitsWidthTable[10]>,
    &BitStream::readSigned<DCC::bitsWidthTable[11]>,
    &BitStream::readSigned<DCC::bitsWidthTable[12]>,
    &BitStream::readSigned<DCC::bitsWidthTable[13]>,
    &BitStream::readSigned<DCC::bitsWidthTable[14]>,
    &BitStream::readSigned<DCC::bitsWidthTable[15]>};

bool DCC::decode(const char* filename)
{
    assert(!stream);
    stream = std::make_unique<FileStream>(filename);
    if (stream && stream->good()) {
        return extractHeaderAndOffsets();
    }
    return false;
}

bool DCC::decode(StreamPtr&& streamPtr)
{
    assert(!stream);
    stream = std::move(streamPtr);
    if (stream && stream->good()) {
        return extractHeaderAndOffsets();
    }
    return false;
}

bool DCC::extractHeaderAndOffsets()
{
    // For now assume the stream is the whole file, and starts at offset 0
    assert(stream->tell() == 0);
    // DCC header can not encode a bigger size anyway
    assert(stream->size() < std::numeric_limits<int32_t>::max());
    stream->readRaw(header.signature);
    stream->readRaw(header.version);
    stream->readRaw(header.directions);
    stream->readRaw(header.framesPerDir);
    stream->readRaw(header.padding0);
    stream->readRaw(header.tag);            // TODO : ENDIAN
    stream->readRaw(header.finalDc6Size);   // TODO : ENDIAN

    assert(header.padding0[0] == 0 && header.padding0[1] == 0 && header.padding0[2] == 0 &&
           "Assumed there are 255 frames max, but Paul Siramy's doc mentions 256 as max ?");

    directionsOffsets.resize(header.directions + 1);
    directionsOffsets[header.directions] = static_cast<uint32_t>(stream->size());
    for (uint32_t dir = 0; dir < header.directions; dir++)
    {
        stream->readRaw(directionsOffsets[dir]); // TODO : ENDIAN
    }
    return stream->good();
}

size_t DCC::getDirectionSize(uint32_t dirIndex)
{
    return directionsOffsets[dirIndex + 1] - directionsOffsets[dirIndex];
}

static bool readDirHeader(DCC::DirectionHeader& dirHeader, BitStream& bitStream)
{
    dirHeader.outsizeCoded          = bitStream.readUnsigned<32>();
    dirHeader.compressColorEncoding = bitStream.readBool();
    dirHeader.compressEqualCells    = bitStream.readBool();
    dirHeader.variable0Bits         = bitStream.readUnsigned<4>();
    dirHeader.widthBits             = bitStream.readUnsigned<4>();
    dirHeader.heightBits            = bitStream.readUnsigned<4>();
    dirHeader.xoffsetBits           = bitStream.readUnsigned<4>();
    dirHeader.yoffsetBits           = bitStream.readUnsigned<4>();
    dirHeader.optionalBytesBits     = bitStream.readUnsigned<4>();
    dirHeader.codedBytesBits        = bitStream.readUnsigned<4>();
    return bitStream.good();
}

static bool readFrameHeaders(uint8_t nbFrames, DCC::Direction& outDir, BitStream& bitStream)
{
    const DCC::DirectionHeader& dirHeader = outDir.header;

    // Read all frame headers
    outDir.frameHeaders.resize(nbFrames);
    for (DCC::FrameHeader& frameHeader : outDir.frameHeaders)
    {
        // We are using member function pointers here because we would have one indirection
        // From looking up the size anyway, so we might as well call the template instance directly
        frameHeader.variable0 = (bitStream.*readUnsignedPtrs[dirHeader.variable0Bits])();
        frameHeader.width     = (bitStream.*readUnsignedPtrs[dirHeader.widthBits])();
        frameHeader.height    = (bitStream.*readUnsignedPtrs[dirHeader.heightBits])();
        frameHeader.xoffset   = (bitStream.*readSignedPtrs[dirHeader.xoffsetBits])();
        frameHeader.yoffset   = (bitStream.*readSignedPtrs[dirHeader.yoffsetBits])();

        frameHeader.optionalBytes = (bitStream.*readUnsignedPtrs[dirHeader.optionalBytesBits])();
        frameHeader.codedBytes    = (bitStream.*readUnsignedPtrs[dirHeader.codedBytesBits])();
        frameHeader.frameBottomUp = bitStream.readBool();

        assert(frameHeader.width < 0x700000);
        assert(frameHeader.height < 0x700000);
        frameHeader.extents.xLower = frameHeader.xoffset;
        frameHeader.extents.xUpper = frameHeader.xoffset + int32_t(frameHeader.width);

        if (frameHeader.frameBottomUp) {
            frameHeader.extents.yLower = frameHeader.yoffset;
            frameHeader.extents.yUpper = frameHeader.yoffset + int32_t(frameHeader.height);
        }
        else // top-down
        {
            frameHeader.extents.yLower = frameHeader.yoffset - int32_t(frameHeader.height) + 1;
            frameHeader.extents.yUpper = frameHeader.yoffset + 1;
        }
    }

    // Handle optional data
    for (DCC::FrameHeader& frameHeader : outDir.frameHeaders)
    {
        if (frameHeader.optionalBytes) {
            bitStream.alignToByte();
            bitStream.skip(frameHeader.optionalBytes * CHAR_BIT);
        }
    }
    return bitStream.good();
}

bool DCC::readDirection(Direction& outDir, uint32_t dirIndex)
{
    using byte                             = unsigned char*;
    const size_t      directionEncodedSize = getDirectionSize(dirIndex);
    std::vector<byte> buffer(directionEncodedSize);
    stream->seek(directionsOffsets[dirIndex], IStream::beg);
    stream->read(buffer.data(), directionEncodedSize);
    assert(stream->good());
    BitStream bitStream(buffer.data(), directionEncodedSize);

    DirectionHeader& dirHeader = outDir.header;
    if (!readDirHeader(dirHeader, bitStream)) return false;

    if (!readFrameHeaders(header.framesPerDir, outDir, bitStream)) return false;

    outDir.computeDirExtents();

    uint32_t equalCellsBitStreamSize    = 0;
    uint32_t pixelMaskBitStreamSize     = 0;
    uint32_t encodingTypeBitsreamSize   = 0;
    uint32_t rawPixelCodesBitStreamSize = 0;

    if (dirHeader.compressEqualCells) {
        equalCellsBitStreamSize = bitStream.readUnsigned<20>();
    }

    pixelMaskBitStreamSize = bitStream.readUnsigned<20>();

    if (dirHeader.compressColorEncoding) {
        encodingTypeBitsreamSize   = bitStream.readUnsigned<20>();
        rawPixelCodesBitStreamSize = bitStream.readUnsigned<20>();
    }
    // Tells what code correspond to which pixel value.
    // For example if the pixel values used are 0, 31 , 42 then
    // code 0 gives 0
    // code 1 gives 31
    // code 2 gives 42
    std::vector<uint8_t> codeToPixelValue;
    for (size_t i = 0; i < 256; i++)
    {
        // Not very efficient but will optimize later if needed
        const bool pixelValueUsed = bitStream.readBool();
        if (pixelValueUsed) codeToPixelValue.push_back(uint8_t(i));
    }

    BitStream equalCellBitStream = bitStream;
    if (dirHeader.compressEqualCells) {
        bitStream.skip(equalCellsBitStreamSize);
    }

    BitStream pixelMaskBitStream = bitStream;
    bitStream.skip(pixelMaskBitStreamSize);

    BitStream encodingTypeBitStream  = bitStream;
    BitStream rawPixelCodesBitStream = bitStream;
    if (dirHeader.compressColorEncoding) {
        bitStream.skip(encodingTypeBitsreamSize);

        rawPixelCodesBitStream.setPosition(bitStream.tell());
        bitStream.skip(rawPixelCodesBitStreamSize);
    }

    // Note : goes until the end of the direction
    BitStream pixelCodesAndDisplacementBitStream = bitStream;

    struct Cell
    {
        size_t width : 4;
        size_t height : 4;
    };

    struct PixelBufferEntry
    {
        uint8_t val[4];
    };

    std::vector<PixelBufferEntry> pixelBuffer;
    WS_UNUSED(equalCellBitStream);
    WS_UNUSED(pixelMaskBitStream);
    WS_UNUSED(encodingTypeBitStream);
    WS_UNUSED(rawPixelCodesBitStream);
    WS_UNUSED(pixelCodesAndDisplacementBitStream);
    WS_UNUSED(pixelBuffer);

    const int32_t dirWidth  = outDir.extents.width();
    const int32_t dirHeight = outDir.extents.height();
    WS_UNUSED(dirWidth);
    WS_UNUSED(dirHeight);

    return bitStream.good();
}
} // namespace WorldStone