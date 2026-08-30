# mksndbnk - makes a soundbank binary for the yf apu
import std/[os, strutils, streams, strformat, endians]

const
  MaxSoundSlots = 64
  SoundbankSize = 65536
  HeaderEntrySize = 12
  TotalHeaderSize = MaxSoundSlots * HeaderEntrySize
  
type
  SndSlotEntry = object
    offset: uint32
    length: uint32
    loopStart: uint16
    volume: uint8
    flags: uint8
    
# wav parser
proc loadWav(path: string): seq[int16] =
  var fs = openFileStream(path, fmRead)
  if fs.isNil: return @[]
  defer: fs.close()
  
  var riffTag = newString(4)
  discard fs.readData(addr riffTag[0], 4)
  if riffTag != "RIFF": return @[]
  
  discard fs.readUint32()
  var waveTag = newString(4)
  discard fs.readData(addr waveTag[0], 4)
  if waveTag != "WAVE": return @[]
  
  var channels: uint16 = 0
  var loopPoint: uint16 = 0
  var bitsPerSample: uint16 = 0
  var dataSize: uint32 = 0
  var dataFound = false
  
  while not fs.atEnd():
    var chunkTag = newString(4)
    if fs.readData(addr chunkTag[0], 4) != 4: break
    let chunkSize = fs.readUint32()
    
    if chunkTag == "fmt ":
      discard fs.readUint16() # audio format (pcm mostly
      channels = fs.readUint16()
      discard fs.readUint32() # sample rate
      discard fs.readUint32() # byte rate
      discard fs.readUint16() # block align
      bitsPerSample = fs.readUint16()
      if chunkSize > 16:
        fs.setPosition(fs.getPosition() + (chunkSize.int - 16))
    elif chunkTag == "data":
      dataSize = chunkSize
      dataFound = true
      break
    else:
      fs.setPosition(fs.getPosition() + chunkSize.int)
      
  if not dataFound or channels == 0: return @[]
  
  let bytesPerSample = (bitsPerSample div 8).int
  let numFrames = dataSize.int div (channels.int * bytesPerSample)
  result = newSeq[int16](numFrames)
  
  for i in 0 ..< numFrames:
    if bitsPerSample == 8:
      let rawU8 = fs.readUint8()
      result[i] = (rawU8.int16 - 128) shl 8
    elif bitsPerSample == 16:
      result[i] = fs.readInt16()
    else:
      return @[]
      
    if channels > 1:
      fs.setPosition(fs.getPosition() + (channels.int - 1) * bytesPerSample)
      
proc encodeBrr(chunk: seq[int16]): array[9, byte] =
  var pChunk: array[16, int16]
  for i in 0 ..< min(chunk.len, 16):
    pChunk[i] = chunk[i]
    
  var bestShift = 12
  for shift in 0 .. 12:
    var valid = true
    for s in pChunk:
      let sample = s.int32
      let step = if shift == 0: sample shl 1 else: sample shr (shift - 1)
      if step < -8 or step > 7:
        valid = false
        break
      if valid:
        bestShift = shift
        break
        
    result[0] = byte((bestShift shl 4) or (0 shl 2))
    
    var byteIdx = 1
    for i in countup(0, 15, 2):
      let s0 = pChunk[i].int32
      let s1 = pChunk[i+1].int32
      
      var step0 = if bestShift == 0: s0 shl 1 else: s0 shr (bestShift - 1)
      var step1 = if bestShift == 0: s1 shl 1 else: s1 shr (bestShift - 1)
      
      step0 = max(-8, min(7, step0))
      step1 = max(-8, min(7, step1))
      
      let nibble0 = byte(step0 and 0x0F)
      let nibble1 = byte(step1 and 0x0F)
      
      result[byteIdx] = (nibble0 shl 4) or nibble1
      inc byteIdx
      
proc packSoundBank(sourceDir, outputPath: string): bool =
  var registry: array[MaxSoundSlots, SndSlotEntry]
  var payload = newSeq[byte]()
  var currentWriteOffset = TotalHeaderSize.uint32
  
  for slotId in 0 ..< MaxSoundSlots:
    let filename = fmt"{slotId:02d}.wav"
    let filepath = sourceDir / filename
    
    if not fileExists(filepath): continue
    
    let pcm = loadWav(filepath)
    if pcm.len == 0: continue
    
    var brrData = newSeq[byte]()
    for i in countup(0, pcm.len - 1, 16):
      let chunkEnd = min(i + 15, pcm.high)
      let blok = encodeBrr(pcm[i .. chunkEnd])
      brrData.add(blok)
    
    let totalBloks = (brrData.len div 9).uint32
    
    if (currentWriteOffset.int + brrData.len) > SoundbankSize:
      echo "Warning: soundbank hit size limit, stopping"
      return false
      
    registry[slotId] = SndSlotEntry(
      offset: currentWriteOffset,
      length: totalBloks,
      loopStart: 0, # TODO: fetch loop points from smpl chunk
      volume: 255,
      flags: 2
    )
    
    payload.add(brrData)
    echo fmt"packed slot [{slotId:02d}]: '{filename}' -> {totalBloks} BRR blocks ({brrData.len} bytes)"
    currentWriteOffset += brrData.len.uint32
    
    
  var outFile = openFileStream(outputPath, fmWrite)
  if outFile.isNil:
    echo "Error: cannot open output file: ", outputPath
    quit(1)
  defer: outFile.close()
  
  for entry in registry:
    outFile.write(entry.offset)
    outFile.write(entry.length)
    outFile.write(entry.loopStart)
    outFile.write(entry.volume)
    outFile.write(entry.flags)
    
  if payload.len > 0:
    outFile.writeData(addr payload[0], payload.len)
    
  let writtenSoFar = outFile.getPosition()
  let paddingNeeded = SoundbankSize - writtenSoFar
  if paddingNeeded > 0:
    var padBuf = newSeq[byte](paddingNeeded)
    for i in 0 ..< paddingNeeded: padBuf[i] = 0x80
    outFile.writeData(addr padBuf[0], paddingNeeded)
    
  echo fmt" packed soundbank successfully: '{outputPath}' ({getFileSize(outputPath)} bytes)"
  return true
  
proc main() =
  let args = commandLineParams()
  if args.len < 2:
    echo "usage: mksndbnk <source_dir> <output.bin>"
    quit(1)
    
  let sourceDir = args[0]
  let outputBIn = args[1]
  
  if not dirExists(sourceDir):
    createDir(sourceDir)
    echo fmt"source directory does not exist, place your sounds in '{sourceDir}' like 00.wav and rerun"
  else:
    if not packSoundbank(sourceDir, outputBin):
      quit(1)
      
main()
