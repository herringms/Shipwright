using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Windows.Forms;

[assembly: AssemblyTitle("Hyrule Co-op")]
[assembly: AssemblyDescription("Hyrule Co-op bootstrap launcher")]
[assembly: AssemblyCompany("Hyrule Co-op contributors")]
[assembly: AssemblyProduct("Hyrule Co-op")]
[assembly: AssemblyVersion("1.1.1.0")]
[assembly: AssemblyFileVersion("1.1.1.0")]

namespace HyruleCoop.Bootstrap {
    internal static class Program {
        private const string EmbeddedVersion = "1.1.1";
        private const string EmbeddedLauncherResource = "HyruleCoop.EmbeddedLauncher";
        private const string EmbeddedConfigurationResource = "HyruleCoop.DefaultConfig";

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

                string embeddedPayloadRoot = InstallEmbeddedPayload(appDataRoot);
                string legacyPayloadRoot = Path.Combine(bootstrapRoot, "_bootstrap");
                string payloadRoot = Directory.Exists(legacyPayloadRoot) ? bootstrapRoot : embeddedPayloadRoot;
                string pointerPath = Path.Combine(appDataRoot, "Launcher", "current.txt");
                string launcherPath = ReadLauncherPointer(pointerPath, appDataRoot);
                if (String.IsNullOrWhiteSpace(launcherPath)) {
                    if (!String.IsNullOrWhiteSpace(embeddedPayloadRoot)) {
                        launcherPath = Path.Combine(embeddedPayloadRoot, "HyruleCoopLauncher.exe");
                    } else {
                        launcherPath = Path.Combine(legacyPayloadRoot, "HyruleCoopLauncher.exe");
                    }
                }

                if (!File.Exists(launcherPath)) {
                    MessageBox.Show(
                        "The Hyrule Co-op launcher could not be installed. Download HyruleCoop.exe again and try again.",
                        "Hyrule Co-op", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return 2;
                }

                List<string> forwarded = new List<string>(args);
                forwarded.Add("--bootstrap-root");
                forwarded.Add(bootstrapRoot);
                if (!String.IsNullOrWhiteSpace(payloadRoot)) {
                    forwarded.Add("--bootstrap-payload-root");
                    forwarded.Add(payloadRoot);
                }

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

        private static string InstallEmbeddedPayload(string appDataRoot) {
            byte[] launcher = ReadResource(EmbeddedLauncherResource);
            if (launcher == null) {
                return null;
            }
            byte[] configuration = ReadResource(EmbeddedConfigurationResource);
            if (configuration == null) {
                throw new InvalidDataException("The embedded Hyrule Co-op configuration is missing.");
            }

            string payloadRoot = Path.Combine(appDataRoot, "Bootstrap", EmbeddedVersion);
            WriteVerifiedResource(Path.Combine(payloadRoot, "HyruleCoopLauncher.exe"), launcher);
            WriteVerifiedResource(Path.Combine(payloadRoot, "_bootstrap", "default-shipofharkinian.json"),
                                  configuration);
            return payloadRoot;
        }

        private static byte[] ReadResource(string name) {
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name)) {
                if (stream == null) {
                    return null;
                }
                using (MemoryStream output = new MemoryStream()) {
                    stream.CopyTo(output);
                    return output.ToArray();
                }
            }
        }

        private static void WriteVerifiedResource(string destination, byte[] contents) {
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            if (File.Exists(destination) && FileMatches(destination, contents)) {
                return;
            }

            string temporary = destination + ".new-" + Process.GetCurrentProcess().Id;
            File.WriteAllBytes(temporary, contents);
            if (File.Exists(destination)) {
                try {
                    File.Replace(temporary, destination, null, true);
                    return;
                } catch (PlatformNotSupportedException) {
                } catch (IOException) {
                }
                File.Delete(destination);
            }
            File.Move(temporary, destination);
        }

        private static bool FileMatches(string path, byte[] expected) {
            using (SHA256 sha = SHA256.Create())
            using (FileStream stream = File.OpenRead(path)) {
                byte[] actualHash = sha.ComputeHash(stream);
                byte[] expectedHash = sha.ComputeHash(expected);
                if (actualHash.Length != expectedHash.Length) {
                    return false;
                }
                for (int index = 0; index < actualHash.Length; index++) {
                    if (actualHash[index] != expectedHash[index]) {
                        return false;
                    }
                }
                return true;
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
