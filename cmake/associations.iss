; Association of .loghunter projects with LogHunter (Inno Setup installer, CPack).
[Registry]
Root: HKA; Subkey: "Software\Classes\.loghunter"; ValueType: string; ValueName: ""; ValueData: "LogHunter.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\LogHunter.Project"; ValueType: string; ValueName: ""; ValueData: "LogHunter project"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\LogHunter.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\LogHunter.exe,0"
Root: HKA; Subkey: "Software\Classes\LogHunter.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\LogHunter.exe"" ""%1"""
