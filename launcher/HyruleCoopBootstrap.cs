using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

[assembly: AssemblyTitle("Hyrule Co-op")]
[assembly: AssemblyDescription("Hyrule Co-op bootstrap launcher")]
[assembly: AssemblyCompany("Hyrule Co-op contributors")]
[assembly: AssemblyProduct("Hyrule Co-op")]
[assembly: AssemblyVersion("1.0.0.0")]
[assembly: AssemblyFileVersion("1.0.0.0")]

namespace HyruleCoop.Bootstrap {
    internal static class Program {
        [STAThread]
        private static int Main(string[] args) {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            try {
                string bootstrapRoot = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
                string appDataRoot = Environment.GetEnvironmentVariable("HYRULE_COOP_ROOT");
                if (String.IsNullOrWhiteSpace(appDataRoot)) {
                    appDataRoot = Path.Combine(
                        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HyruleCoop");
                }

                string pointerPath = Path.Combine(appDataRoot, "Launcher", "current.txt");
                string launcherPath = ReadLauncherPointer(pointerPath, appDataRoot);
                if (String.IsNullOrWhiteSpace(launcherPath)) {
                    launcherPath = Path.Combine(bootstrapRoot, "_bootstrap", "HyruleCoopLauncher.exe");
                }

                if (!File.Exists(launcherPath)) {
                    MessageBox.Show(
                        "The Hyrule Co-op launcher is missing. Re-extract the complete bootstrap ZIP and try again.",
                        "Hyrule Co-op", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return 2;
                }

                List<string> forwarded = new List<string>(args);
                forwarded.Add("--bootstrap-root");
                forwarded.Add(bootstrapRoot);

                ProcessStartInfo startInfo = new ProcessStartInfo();
                startInfo.FileName = launcherPath;
                startInfo.Arguments = JoinArguments(forwarded);
                startInfo.WorkingDirectory = Path.GetDirectoryName(launcherPath);
                startInfo.UseShellExecute = false;
                Process.Start(startInfo);
                return 0;
            } catch (Exception ex) {
                MessageBox.Show(ex.Message, "Hyrule Co-op could not start", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        private static string ReadLauncherPointer(string pointerPath, string appDataRoot) {
            try {
                if (!File.Exists(pointerPath)) {
                    return null;
                }
                string candidate = File.ReadAllText(pointerPath).Trim();
                if (String.IsNullOrWhiteSpace(candidate)) {
                    return null;
                }
                candidate = Path.GetFullPath(candidate);
                string launcherRoot = Path.GetFullPath(Path.Combine(appDataRoot, "Launcher")) + Path.DirectorySeparatorChar;
                if (!candidate.StartsWith(launcherRoot, StringComparison.OrdinalIgnoreCase) || !File.Exists(candidate)) {
                    return null;
                }
                return candidate;
            } catch {
                return null;
            }
        }

        private static string JoinArguments(IEnumerable<string> arguments) {
            List<string> quoted = new List<string>();
            foreach (string argument in arguments) {
                quoted.Add(QuoteArgument(argument));
            }
            return String.Join(" ", quoted.ToArray());
        }

        private static string QuoteArgument(string value) {
            if (value == null) {
                return "\"\"";
            }

            StringBuilder output = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in value) {
                if (character == '\\') {
                    backslashes++;
                    continue;
                }
                if (character == '"') {
                    output.Append('\\', backslashes * 2 + 1);
                    output.Append('"');
                } else {
                    output.Append('\\', backslashes);
                    output.Append(character);
                }
                backslashes = 0;
            }
            output.Append('\\', backslashes * 2);
            output.Append('"');
            return output.ToString();
        }
    }
}
