cl /MD /O1 /GL /Gy /GR- /EHsc /W4 /I include src\\vss_client.cpp src\\privileges.cpp src\\path_utils.cpp src\\zip_writer.cpp src\\snapshot_utils.cpp src\\file_utils.cpp src\\upload_utils.cpp /Fe:vss_client.exe vssapi.lib ole32.lib oleaut32.lib advapi32.lib winhttp.lib /link /MACHINE:X64 /LTCG /OPT:REF /OPT:ICF /RELEASE

if exist *.obj del *.obj
