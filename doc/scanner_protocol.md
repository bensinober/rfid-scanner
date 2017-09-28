# RFID Scanner http protocol
```
HEADER:
Bytes  Desc
2      0x4242 (a protocol number)
6      MAC ADDRESS of the rfid device
32     shelf
[one or more RECORDs]

RECORD:
8      RFID tag id
1      flags (bitmask: 1 => checkin)
1      data length (N)
N      data
```
After the scanner found 1 or more RFID tags, it will send a request to the server with a header and one or more records.

records might contains rfid tags related to items, or shelf tags.

each tags following a shelf tag belongs to that shelf, and not the initial shelf value.

for a shelf, the data will contains `SHELF#shelf.name.here`

# RESPONSE

The server might respond with:

## NOOP
```
NOOP\n
$barcode\n
$author\n
$title\n
$callnum\n
$location\n
```
There is nothing to do (the server just return the information of the first record, which can be useful only to verify that everything is working or identify a lonely tag)

## PICK
```
PICK\n
$barcode\n
$author\n
$title\n
$callnum\n
$location\n
```
One of the books need to be picked up. The destination of the book will be written in the $location.

## READ
```
READ\n
$rfid (8 bytes)\n
```
The scanner need to read more data from the given RFID tag id (usually when the scanner didn’t send any data, but there is no mapping of the RFID tag id on the server).

The scanner should re-scan (Reset To Ready) the tag, and be sure to read enough block data from the tag.

## WRITE
```
WRT\n
length($rfid+$data)\n
$rfid+$data
```
The scanner need to overwrite the tag with the given RFID id, with $data. (the $rfid is 8 bytes and is the rfid id that need to be written, while $data is the data to be written)

## IMG
```
IMG\n
base64 128 chars\n
base64 128 chars\n
base64 128 chars\n
base64 128 chars\n
base64 128 chars\n
base64 128 chars\n
```

display a custom image on the display, top left bixel is the lowest bit of the first byte of the first line.

The main reason for doing this is to use unicode letters, which are too cumbersome to upload as a font on some devices.

the size is 128x32

## PIMG

same as IMG, but higher display priority and expire, and it will trigger a sound like PICK
