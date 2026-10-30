# protocol

Specification of the UART link between the Linux brain and the ESP32-S3.
Newline-terminated ASCII lines at 921600 baud, commands Linux->ESP32 and events ESP32->Linux.
The spec lives in [PROTOCOL.md](PROTOCOL.md); both `firmware/` and `brain/` implement it.
Change the spec first, then the code.
[test_vectors.txt](test_vectors.txt) holds shared VALID/INVALID cases; the Python tests (`brain/tests/`)
and the C++ tests (`firmware/test/`) both run every vector, so the two implementations must agree.
