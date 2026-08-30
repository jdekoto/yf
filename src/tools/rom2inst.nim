import std/[os, strutils, streams]

proc main() =
  let args = commandLineParams()
  if args.len < 2:
    echo: "Usage: rom2inst <input.rom> <output.luac>"
    quit(1)
  	
let inputPath = args(0)
let outputPath = args(1)

var fs = openFileStream(inputPath, fmRead)
if fs.isNil:
  echo "Error: cannot open input file: ", inputPath
  quit(1)
defer: fs.close()

var magic = newString(4)
if fs.readData(addr magic[0], 4) != 4 or magic != "YFC!":
  echo "Error: invalid ROM, needs the 'YFC!' signature"
  quit(1)
 
var romId = newString(8)
var romName = newString(32)
var romAuthor = newString(32)
var romVersion = newString(8)

discard fs.readData(addr romId[0], 8)
discard fs.readData(addr romName[0], 32)
discard fs.readData(addr romAuthor[0], 32)
discard fs.readData(addr romVersion[0], 8)

let bytecodeLen = fs.readUint32()

var payload = newSeq[byte](bytecodeLen)
if bytecodeLen > 0 and fs.readData(addr payload[0], bytecodeLen.int) != bytecodeLen.int:
  echo "Error: the bytecode stream doesnt look right at all (truncated or corrupted)"
  quit(1)
  
var outFile = openFileStream(outputPath, fmWrite)
if outFile.isNil:
  echo "Error: cannot open output file: ", outputPath
  quit(1)
defer: outFile.close()

if bytecodeLen > 0:
  outFile.writeData(addr payload[0], bytecodeLen.int)
  
echo "extracted ROM successfully!"
echo "title:   ", romName.strip(chars = {'\0'})
echo "author:  ", romAuthor.strip(chars = {'\0'})
echo "id:      ", romId.strip(chars = {'\0'})
echo "version: ", romVersion.strip(chars = {'\0'})
echo "payload: ", bytecodeLen " bytes -> ", outputPath

main()
