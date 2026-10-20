# Third-party notices

sys-bar itself (`sys-bar.exe`) uses only Windows system libraries.

The optional **advanced sensors** component (`sensors\`, x64 only) ships the following third-party software.
Each component keeps its own license; its source code is available at the links below.

| Component | Version | License | Source |
| --- | --- | --- | --- |
| LibreHardwareMonitorLib | 0.9.6 | MPL-2.0 | https://github.com/LibreHardwareMonitor/LibreHardwareMonitor |
| DiskInfoToolkit | (dependency) | MPL-2.0 | https://github.com/Blacktempel/DiskInfoToolkit |
| RAMSPDToolkit-NDD | (dependency) | MPL-2.0 | https://github.com/Blacktempel/RAMSPDToolkit |
| BlackSharp.Core | (dependency) | MPL-2.0 | https://github.com/Blacktempel/BlackSharp |
| HidSharp | 2.6.4 | Apache-2.0 | https://www.zer7.com/software/hidsharp |
| System.Buffers, System.Memory, System.Numerics.Vectors, System.Runtime.CompilerServices.Unsafe, System.CodeDom, System.Security.AccessControl, System.Security.Principal.Windows, System.Threading.AccessControl, System.Management | (dependencies) | MIT | https://github.com/dotnet/runtime |
| PawnIO driver installer (`PawnIO_setup.exe`) | 2.2.0 | GPL-2.0 (driver) | https://github.com/namazso/PawnIO, https://github.com/namazso/PawnIO.Setup |

The binaries above are unmodified. They are used as separate libraries (MPL-2.0 / Apache-2.0 / MIT).
PawnIO is a separate program, installed with its official, unmodified installer and only when the user
enables advanced sensors.

The full license texts are available at:

- MPL-2.0: https://www.mozilla.org/MPL/2.0/
- Apache-2.0: https://www.apache.org/licenses/LICENSE-2.0
- MIT: https://opensource.org/license/mit
- GPL-2.0: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html
