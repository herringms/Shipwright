using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows.Forms;

namespace HyruleCoop.Launcher {
    internal sealed class RuntimeFileEntry {
        public string path { get; set; }
        public string sha256 { get; set; }
        public long size { get; set; }
    }

    internal sealed class ReleaseManifest {
        public int schemaVersion { get; set; }
        public string channel { get; set; }
        public string releaseId { get; set; }
        public string version { get; set; }
        public string publishedUtc { get; set; }
        public string compatibilityId { get; set; }
        public int assetSchema { get; set; }
        public string minimumLauncherVersion { get; set; }
        public string launcherVersion { get; set; }
        public string launcherUrl { get; set; }
        public string launcherSha256 { get; set; }
        public string runtimeUrl { get; set; }
        public string runtimeArchive { get; set; }
        public string runtimeSha256 { get; set; }
        public long runtimeSize { get; set; }
        public RuntimeFileEntry[] files { get; set; }
    }

    internal sealed class ProgressUpdate {
        public string Message;
        public int Percent;
        public bool Indeterminate;
    }

    internal sealed class LaunchOutcome {
        public string GameExecutable;
        public string WorkingDirectory;
        public string ReleaseId;
        public string ReplacementLauncherPath;
        public bool RelaunchLauncher;
        public string WarningMessage;
    }

    internal sealed class ExistingInstallation {
        public string Path;
        public string O2rPath;
        public string O2rHash;

        public override string ToString() {
            string label = Path;
            if (!String.IsNullOrWhiteSpace(O2rHash)) {
                label += "  [O2R " + O2rHash.Substring(0, 12) + "]";
            }
            return label;
        }
    }

    internal static class Hashing {
        public static string Sha256(string path) {
            using (FileStream stream = File.OpenRead(path))
            using (SHA256 sha = SHA256.Create()) {
                byte[] hash = sha.ComputeHash(stream);
                StringBuilder output = new StringBuilder(hash.Length * 2);
                foreach (byte value in hash) {
                    output.Append(value.ToString("X2"));
                }
                return output.ToString();
            }
        }
    }

    internal static class AtomicFile {
        public static void WriteAllText(string path, string contents) {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temporary = path + ".new-" + Process.GetCurrentProcess().Id;
            File.WriteAllText(temporary, contents, new UTF8Encoding(false));
            if (File.Exists(path)) {
                string backup = path + ".previous";
                try {
                    File.Replace(temporary, path, backup, true);
                    if (File.Exists(backup)) {
                        File.Delete(backup);
                    }
                    return;
                } catch (PlatformNotSupportedException) {
                } catch (IOException) {
                }
                File.Delete(path);
            }
            File.Move(temporary, path);
        }
    }

    internal sealed class LauncherEngine {
        private const string DefaultManifestUrl =
            "https://github.com/herringms/Shipwright/releases/latest/download/hyrule-coop-release.json";
        private const string ExpectedCompatibilityId = "hyrule-coop-poc.3";
        private static readonly Regex SafeReleaseId = new Regex("^[A-Za-z0-9._-]{1,80}$", RegexOptions.Compiled);
        private static readonly Regex Sha256Pattern = new Regex("^[A-Fa-f0-9]{64}$", RegexOptions.Compiled);
        private static readonly Regex SemanticVersionPattern =
            new Regex("^\\d+\\.\\d+\\.\\d+$", RegexOptions.Compiled);

        private readonly LauncherOptions options;
        private readonly string launcherVersion;
        private readonly string rootPath;
        private readonly string runtimeRoot;
        private readonly string launcherRoot;
        private readonly string bootstrapPayloadRoot;
        private readonly string userDataRoot;
        private readonly string updateRoot;
        private readonly string logPath;
        private readonly JavaScriptSerializer serializer = new JavaScriptSerializer();
        private string pendingRomPath;

        public int ExitCode { get; private set; }

        public LauncherEngine(LauncherOptions launcherOptions, string currentLauncherVersion) {
            options = launcherOptions;
            launcherVersion = currentLauncherVersion;
            rootPath = options.RootPath;
            runtimeRoot = Path.Combine(rootPath, "Runtime");
            launcherRoot = Path.Combine(rootPath, "Launcher");
            bootstrapPayloadRoot = String.IsNullOrWhiteSpace(options.BootstrapPayloadRoot)
                ? options.BootstrapRoot
                : options.BootstrapPayloadRoot;
            userDataRoot = Path.Combine(rootPath, "UserData");
            updateRoot = Path.Combine(rootPath, "Updates");
            logPath = Path.Combine(launcherRoot, "launcher.log");
            ExitCode = 0;
            EnsureDirectories();
        }

        public int RunHeadless() {
            try {
                if (!String.IsNullOrWhiteSpace(options.ImportCandidate)) {
                    ExistingInstallation candidate = BuildCandidate(options.ImportCandidate);
                    if (candidate == null) {
                        throw new InvalidOperationException("The requested import candidate is not a Shipwright installation.");
                    }
                    ImportCandidate(candidate, null, true);
                }
                EnsureDefaultConfiguration();
                LaunchOutcome outcome = Run(null);
                if (!options.NoLaunch && outcome != null && outcome.GameExecutable != null) {
                    StartGame(outcome);
                }
                return 0;
            } catch (Exception ex) {
                RecordFailure(ex);
                return 1;
            }
        }

        public void RunFirstSetup(IWin32Window owner) {
            List<ExistingInstallation> candidates = options.SkipImport
                ? new List<ExistingInstallation>()
                : DiscoverExistingInstallations();
            MigratePreferences(candidates);

            if (options.SkipImport || File.Exists(Path.Combine(rootPath, "migration-v1.json"))) {
                EnsureDefaultConfiguration();
                return;
            }

            if (File.Exists(Path.Combine(userDataRoot, "oot.o2r")) ||
                File.Exists(Path.Combine(userDataRoot, "oot-mq.o2r"))) {
                WriteMigrationMarker("existing AppData user data");
                EnsureDefaultConfiguration();
                return;
            }

            using (SetupDialog dialog = new SetupDialog(candidates)) {
                DialogResult result = dialog.ShowDialog(owner);
                if (result == DialogResult.OK && dialog.SelectedInstallation != null) {
                    ImportCandidate(dialog.SelectedInstallation, owner, false);
                } else if (result == DialogResult.Yes && !String.IsNullOrWhiteSpace(dialog.SelectedRomPath)) {
                    ImportRom(dialog.SelectedRomPath, owner);
                    WriteMigrationMarker("user-selected ROM");
                } else {
                    Log("Initial asset import was deferred by the player.");
                }
            }
            EnsureDefaultConfiguration();
        }

        private void EnsureDefaultConfiguration() {
            string destination = Path.Combine(userDataRoot, "shipofharkinian.json");
            if (File.Exists(destination)) {
                return;
            }
            string contents = ReadBootstrapDefaultConfiguration();
            if (!String.IsNullOrWhiteSpace(contents)) {
                AtomicFile.WriteAllText(destination, contents);
            }
        }

        public LaunchOutcome Run(BackgroundWorker worker) {
            Report(worker, "Checking the installed Hyrule Co-op runtime...", 0, true);

            if (options.Rollback) {
                RollbackRuntime();
            }

            string currentReleaseId = ReadReleasePointer("current-runtime.txt");
            ReleaseManifest currentManifest = LoadInstalledManifest(currentReleaseId);
            ReleaseManifest availableManifest = null;

            if (!options.Offline) {
                try {
                    string source = options.ManifestOverride;
                    if (String.IsNullOrWhiteSpace(source)) {
                        source = Environment.GetEnvironmentVariable("HYRULE_COOP_UPDATE_MANIFEST_URL");
                    }
                    if (String.IsNullOrWhiteSpace(source)) {
                        source = DefaultManifestUrl;
                    }
                    Report(worker, "Checking GitHub Releases for an update...", 0, true);
                    availableManifest = LoadManifest(source);
                    ValidateManifest(availableManifest, true);
                } catch (Exception ex) {
                    Log("Update check failed; installed runtime remains available. " + ex.Message);
                    availableManifest = null;
                }
            }

            if (availableManifest == null && currentManifest == null) {
                availableManifest = LoadBootstrapManifest();
                if (availableManifest != null) {
                    ValidateManifest(availableManifest, false);
                }
            }

            if (availableManifest != null) {
                try {
                    string replacementLauncher = InstallReplacementLauncherIfRequired(availableManifest, worker);
                    if (!String.IsNullOrWhiteSpace(replacementLauncher)) {
                        LaunchOutcome relaunch = new LaunchOutcome();
                        relaunch.RelaunchLauncher = true;
                        relaunch.ReplacementLauncherPath = replacementLauncher;
                        return relaunch;
                    }
                } catch (Exception ex) {
                    if (currentManifest == null) {
                        throw;
                    }
                    Log("Launcher update failed; retaining the installed runtime. " + ex.Message);
                    availableManifest = null;
                }
            }

            if (ShouldInstall(availableManifest, currentManifest)) {
                try {
                    currentReleaseId = InstallRuntime(availableManifest, worker);
                    currentManifest = availableManifest;
                } catch (OperationCanceledException) {
                    Log("Update cancelled by the player.");
                    if (currentManifest == null) {
                        throw;
                    }
                } catch (Exception ex) {
                    if (currentManifest == null) {
                        throw;
                    }
                    Log("Runtime update failed; retaining " + currentManifest.releaseId + ". " + ex.Message);
                }
            }

            if (currentManifest == null || String.IsNullOrWhiteSpace(currentReleaseId)) {
                throw new InvalidOperationException(
                    "No verified Hyrule Co-op runtime is installed, and the bootstrap or GitHub release could not be loaded.");
            }

            string runtimeDirectory = RuntimeDirectory(currentReleaseId);
            string runtimeWarning = null;
            try {
                ValidateInstalledRuntime(runtimeDirectory, currentManifest);
            } catch (Exception ex) {
                string failedRelease = currentReleaseId;
                string previousRelease = ReadReleasePointer("previous-runtime.txt");
                ReleaseManifest previousManifest = LoadInstalledManifest(previousRelease);
                if (previousManifest == null) {
                    throw;
                }
                string previousDirectory = RuntimeDirectory(previousRelease);
                ValidateInstalledRuntime(previousDirectory, previousManifest);
                AtomicFile.WriteAllText(Path.Combine(rootPath, "current-runtime.txt"), previousRelease);
                AtomicFile.WriteAllText(Path.Combine(rootPath, "failed-runtime.txt"), failedRelease);
                string previousPointer = Path.Combine(rootPath, "previous-runtime.txt");
                if (File.Exists(previousPointer)) {
                    File.Delete(previousPointer);
                }
                currentReleaseId = previousRelease;
                currentManifest = previousManifest;
                runtimeDirectory = previousDirectory;
                runtimeWarning = "Hyrule Co-op detected that runtime " + failedRelease +
                                 " failed verification and restored " + previousRelease + ".";
                Log(runtimeWarning + " " + ex.Message);
            }
            string assetWarning = UpdateAssetMetadata(currentManifest, runtimeDirectory);
            Report(worker, options.NoLaunch ? "Runtime verified." : "Starting Hyrule Co-op...", 100, false);

            LaunchOutcome outcome = new LaunchOutcome();
            outcome.ReleaseId = currentReleaseId;
            outcome.WorkingDirectory = userDataRoot;
            outcome.WarningMessage = JoinWarnings(runtimeWarning, assetWarning);
            if (!options.NoLaunch) {
                outcome.GameExecutable = Path.Combine(runtimeDirectory, "soh.exe");
            }
            return outcome;
        }

        public void StartReplacementLauncher(string replacementPath) {
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = replacementPath;
            startInfo.Arguments = options.ForwardedArguments(true);
            startInfo.WorkingDirectory = Path.GetDirectoryName(replacementPath);
            startInfo.UseShellExecute = false;
            Process.Start(startInfo);
        }

        public void StartGame(LaunchOutcome outcome) {
            try {
                Directory.CreateDirectory(outcome.WorkingDirectory);
                ProcessStartInfo startInfo = new ProcessStartInfo();
                startInfo.FileName = outcome.GameExecutable;
                startInfo.WorkingDirectory = outcome.WorkingDirectory;
                startInfo.UseShellExecute = false;
                startInfo.EnvironmentVariables["HYRULE_COOP_RELEASE_ID"] = outcome.ReleaseId;

                string romPath = pendingRomPath;
                string pendingFile = Path.Combine(rootPath, "pending-rom.txt");
                if (String.IsNullOrWhiteSpace(romPath) && File.Exists(pendingFile)) {
                    romPath = File.ReadAllText(pendingFile).Trim();
                }
                if (!String.IsNullOrWhiteSpace(romPath) && File.Exists(romPath) &&
                    !File.Exists(Path.Combine(userDataRoot, "oot.o2r")) &&
                    !File.Exists(Path.Combine(userDataRoot, "oot-mq.o2r"))) {
                    startInfo.Arguments = ArgumentQuoting.Quote(romPath);
                }

                Process.Start(startInfo);
                Log("Started runtime " + outcome.ReleaseId + " from " + outcome.GameExecutable);
            } catch (Exception ex) {
                string previous = ReadReleasePointer("previous-runtime.txt");
                if (!String.IsNullOrWhiteSpace(previous) &&
                    MessageBox.Show("The current runtime could not start. Restore the previous version?\n\n" + ex.Message,
                                    "Hyrule Co-op", MessageBoxButtons.YesNo, MessageBoxIcon.Error) == DialogResult.Yes) {
                    AtomicFile.WriteAllText(Path.Combine(rootPath, "current-runtime.txt"), previous);
                    ReleaseManifest manifest = LoadInstalledManifest(previous);
                    LaunchOutcome fallback = new LaunchOutcome();
                    fallback.ReleaseId = previous;
                    fallback.WorkingDirectory = userDataRoot;
                    fallback.GameExecutable = Path.Combine(RuntimeDirectory(previous), "soh.exe");
                    StartGame(fallback);
                    return;
                }
                throw;
            }
        }

        public void RecordFailure(Exception ex) {
            ExitCode = 1;
            Log("ERROR: " + ex);
        }

        private void EnsureDirectories() {
            Directory.CreateDirectory(rootPath);
            Directory.CreateDirectory(runtimeRoot);
            Directory.CreateDirectory(launcherRoot);
            Directory.CreateDirectory(userDataRoot);
            Directory.CreateDirectory(updateRoot);
            Directory.CreateDirectory(Path.Combine(rootPath, "Save"));
            Directory.CreateDirectory(Path.Combine(userDataRoot, "mods"));
            Directory.CreateDirectory(Path.Combine(userDataRoot, "logs"));
        }

        private ReleaseManifest LoadManifest(string source) {
            string json;
            if (File.Exists(source)) {
                json = File.ReadAllText(Path.GetFullPath(source));
            } else {
                Uri uri;
                if (!Uri.TryCreate(source, UriKind.Absolute, out uri) || !IsAllowedDownloadUri(uri)) {
                    throw new InvalidOperationException("The update manifest location is not a permitted HTTPS or local URL.");
                }
                ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
                using (WebClient client = CreateWebClient()) {
                    json = client.DownloadString(uri);
                }
            }
            ReleaseManifest manifest = serializer.Deserialize<ReleaseManifest>(json);
            if (manifest == null) {
                throw new InvalidDataException("The update manifest is empty.");
            }
            return manifest;
        }

        private ReleaseManifest LoadBootstrapManifest() {
            if (String.IsNullOrWhiteSpace(bootstrapPayloadRoot)) {
                return null;
            }
            string path = Path.Combine(bootstrapPayloadRoot, "_bootstrap", "hyrule-coop-release.json");
            if (!File.Exists(path)) {
                return null;
            }
            return serializer.Deserialize<ReleaseManifest>(File.ReadAllText(path));
        }

        private ReleaseManifest LoadInstalledManifest(string releaseId) {
            if (String.IsNullOrWhiteSpace(releaseId) || !SafeReleaseId.IsMatch(releaseId)) {
                return null;
            }
            string path = Path.Combine(RuntimeDirectory(releaseId), "hyrule-coop-release.json");
            if (!File.Exists(path)) {
                return null;
            }
            try {
                ReleaseManifest manifest = serializer.Deserialize<ReleaseManifest>(File.ReadAllText(path));
                ValidateManifest(manifest, false);
                return manifest;
            } catch (Exception ex) {
                Log("Installed manifest is invalid: " + ex.Message);
                return null;
            }
        }

        private void ValidateManifest(ReleaseManifest manifest, bool remote) {
            if (manifest == null || manifest.schemaVersion != 1 ||
                String.IsNullOrWhiteSpace(manifest.releaseId) || !SafeReleaseId.IsMatch(manifest.releaseId) ||
                !SemanticVersionPattern.IsMatch(manifest.version ?? "") ||
                !String.Equals(manifest.compatibilityId, ExpectedCompatibilityId, StringComparison.Ordinal) ||
                manifest.assetSchema < 1 || manifest.runtimeSize < 1 || manifest.files == null ||
                manifest.files.Length == 0 || !Sha256Pattern.IsMatch(manifest.runtimeSha256 ?? "") ||
                !SemanticVersionPattern.IsMatch(manifest.minimumLauncherVersion ?? "") ||
                !SemanticVersionPattern.IsMatch(manifest.launcherVersion ?? "") ||
                CompareVersions(manifest.launcherVersion, manifest.minimumLauncherVersion) < 0) {
                throw new InvalidDataException("Unsupported or incomplete Hyrule Co-op release manifest.");
            }
            if (remote && String.IsNullOrWhiteSpace(manifest.runtimeUrl)) {
                throw new InvalidDataException("The remote release manifest has no runtime URL.");
            }

            HashSet<string> paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (RuntimeFileEntry entry in manifest.files) {
                if (entry == null || !IsSafeRelativePath(entry.path) || !Sha256Pattern.IsMatch(entry.sha256 ?? "") ||
                    entry.size < 0 || !paths.Add(entry.path.Replace('\\', '/'))) {
                    throw new InvalidDataException("The release manifest contains an invalid runtime file entry.");
                }
                if (IsPlayerOwnedPath(entry.path)) {
                    throw new InvalidDataException("The release attempts to manage player-owned data: " + entry.path);
                }
            }
            if (!paths.Contains("soh.exe") || !paths.Contains("soh.o2r") ||
                !paths.Contains("extractor-assets.zip")) {
                throw new InvalidDataException("The release does not contain a complete Hyrule Co-op runtime.");
            }
        }

        private bool ShouldInstall(ReleaseManifest available, ReleaseManifest current) {
            if (available == null) {
                return false;
            }
            if (current == null) {
                return true;
            }
            if (String.Equals(available.releaseId, current.releaseId, StringComparison.OrdinalIgnoreCase)) {
                return false;
            }
            return CompareVersions(available.version, current.version) > 0 || !String.IsNullOrWhiteSpace(options.ManifestOverride);
        }

        private string InstallReplacementLauncherIfRequired(ReleaseManifest manifest, BackgroundWorker worker) {
            if (String.IsNullOrWhiteSpace(manifest.launcherVersion) ||
                CompareVersions(manifest.launcherVersion, launcherVersion) <= 0) {
                if (!String.IsNullOrWhiteSpace(manifest.minimumLauncherVersion) &&
                    CompareVersions(launcherVersion, manifest.minimumLauncherVersion) < 0) {
                    throw new InvalidOperationException("This release requires a newer Hyrule Co-op launcher.");
                }
                return null;
            }
            if (String.IsNullOrWhiteSpace(manifest.launcherUrl) ||
                !Sha256Pattern.IsMatch(manifest.launcherSha256 ?? "")) {
                throw new InvalidDataException("The release requires a launcher update but does not provide one.");
            }

            string destinationDirectory = Path.Combine(launcherRoot, manifest.launcherVersion);
            string destination = Path.Combine(destinationDirectory, "HyruleCoopLauncher.exe");
            Directory.CreateDirectory(destinationDirectory);
            if (!File.Exists(destination) ||
                !String.Equals(Hashing.Sha256(destination), manifest.launcherSha256,
                               StringComparison.OrdinalIgnoreCase)) {
                string temporary = Path.Combine(updateRoot, "launcher-" + manifest.launcherVersion + ".download");
                DownloadFile(manifest.launcherUrl, temporary, worker, "Downloading launcher update");
                VerifyHash(temporary, manifest.launcherSha256, "launcher update");
                File.Copy(temporary, destination, true);
                File.Delete(temporary);
            }
            AtomicFile.WriteAllText(Path.Combine(launcherRoot, "current.txt"), destination);
            Log("Installed launcher " + manifest.launcherVersion);
            return destination;
        }

        private string InstallRuntime(ReleaseManifest manifest, BackgroundWorker worker) {
            ValidateManifest(manifest, !String.IsNullOrWhiteSpace(manifest.runtimeUrl));
            Report(worker, "Preparing Hyrule Co-op " + manifest.version + "...", 0, true);

            string archivePath = Path.Combine(updateRoot, manifest.releaseId + ".zip");
            string localArchive = ResolveBootstrapArchive(manifest);
            if (!String.IsNullOrWhiteSpace(localArchive)) {
                File.Copy(localArchive, archivePath, true);
                Report(worker, "Verifying the bundled runtime...", 40, false);
            } else {
                DownloadFile(manifest.runtimeUrl, archivePath, worker, "Downloading Hyrule Co-op " + manifest.version);
            }
            VerifyHash(archivePath, manifest.runtimeSha256, "runtime archive");
            if (new FileInfo(archivePath).Length != manifest.runtimeSize) {
                throw new InvalidDataException("The runtime archive size does not match the release manifest.");
            }

            string finalDirectory = RuntimeDirectory(manifest.releaseId);
            if (Directory.Exists(finalDirectory)) {
                try {
                    ValidateInstalledRuntime(finalDirectory, manifest);
                    SetCurrentRuntime(manifest.releaseId);
                    return manifest.releaseId;
                } catch {
                    SafeDeleteDirectory(finalDirectory, runtimeRoot);
                }
            }

            string stagingDirectory = finalDirectory + ".staging-" + Process.GetCurrentProcess().Id;
            if (Directory.Exists(stagingDirectory)) {
                SafeDeleteDirectory(stagingDirectory, runtimeRoot);
            }
            Directory.CreateDirectory(stagingDirectory);
            try {
                Report(worker, "Installing the verified runtime...", 80, false);
                ExtractArchiveSafely(archivePath, stagingDirectory);
                ValidateInstalledRuntime(stagingDirectory, manifest);
                File.WriteAllText(Path.Combine(stagingDirectory, "hyrule-coop-release.json"),
                                  serializer.Serialize(manifest), new UTF8Encoding(false));
                Directory.Move(stagingDirectory, finalDirectory);
            } catch {
                if (Directory.Exists(stagingDirectory)) {
                    SafeDeleteDirectory(stagingDirectory, runtimeRoot);
                }
                throw;
            }

            SetCurrentRuntime(manifest.releaseId);
            Log("Installed runtime " + manifest.releaseId);
            return manifest.releaseId;
        }

        private void SetCurrentRuntime(string releaseId) {
            string current = ReadReleasePointer("current-runtime.txt");
            Log("Switching runtime pointer from " + (current ?? "<none>") + " to " + releaseId);
            if (!String.IsNullOrWhiteSpace(current) &&
                !String.Equals(current, releaseId, StringComparison.OrdinalIgnoreCase)) {
                AtomicFile.WriteAllText(Path.Combine(rootPath, "previous-runtime.txt"), current);
            }
            AtomicFile.WriteAllText(Path.Combine(rootPath, "current-runtime.txt"), releaseId);
        }

        private void RollbackRuntime() {
            string current = ReadReleasePointer("current-runtime.txt");
            string previous = ReadReleasePointer("previous-runtime.txt");
            if (String.IsNullOrWhiteSpace(previous) || LoadInstalledManifest(previous) == null) {
                throw new InvalidOperationException("No previous Hyrule Co-op runtime is available for rollback.");
            }
            AtomicFile.WriteAllText(Path.Combine(rootPath, "current-runtime.txt"), previous);
            if (!String.IsNullOrWhiteSpace(current)) {
                AtomicFile.WriteAllText(Path.Combine(rootPath, "previous-runtime.txt"), current);
            }
            Log("Rolled back from " + current + " to " + previous);
        }

        private void ValidateInstalledRuntime(string directory, ReleaseManifest manifest) {
            string root = Path.GetFullPath(directory) + Path.DirectorySeparatorChar;
            HashSet<string> expected = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (RuntimeFileEntry entry in manifest.files) {
                string path = Path.GetFullPath(Path.Combine(directory, entry.path));
                if (!path.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(path)) {
                    throw new InvalidDataException("The runtime is missing " + entry.path + ".");
                }
                expected.Add(entry.path.Replace('\\', '/'));
                FileInfo info = new FileInfo(path);
                if (info.Length != entry.size ||
                    !String.Equals(Hashing.Sha256(path), entry.sha256, StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidDataException("The runtime file failed verification: " + entry.path);
                }
            }

            foreach (string file in Directory.GetFiles(directory, "*", SearchOption.AllDirectories)) {
                string fullPath = Path.GetFullPath(file);
                if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase)) {
                    throw new InvalidDataException("The runtime contains a file outside its version directory.");
                }
                string relative = fullPath.Substring(root.Length).Replace('\\', '/');
                if (!expected.Contains(relative) &&
                    !String.Equals(relative, "hyrule-coop-release.json", StringComparison.OrdinalIgnoreCase) &&
                    !IsPlayerOwnedPath(relative)) {
                    throw new InvalidDataException("The runtime contains an unmanaged file: " + relative);
                }
            }
        }

        private void DownloadFile(string source, string destination, BackgroundWorker worker, string message) {
            Uri uri;
            if (File.Exists(source)) {
                File.Copy(Path.GetFullPath(source), destination, true);
                return;
            }
            if (!Uri.TryCreate(source, UriKind.Absolute, out uri) || !IsAllowedDownloadUri(uri)) {
                throw new InvalidOperationException("A release file uses an unsupported download URL.");
            }

            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12;
            using (WebClient client = CreateWebClient())
            using (ManualResetEvent complete = new ManualResetEvent(false)) {
                Exception failure = null;
                bool cancelled = false;
                client.DownloadProgressChanged += delegate(object sender, DownloadProgressChangedEventArgs args) {
                    Report(worker, message + " (" + FormatBytes(args.BytesReceived) + " of " +
                                   FormatBytes(args.TotalBytesToReceive) + ")", args.ProgressPercentage, false);
                };
                client.DownloadFileCompleted += delegate(object sender, AsyncCompletedEventArgs args) {
                    failure = args.Error;
                    cancelled = args.Cancelled;
                    complete.Set();
                };
                client.DownloadFileAsync(uri, destination);
                while (!complete.WaitOne(100)) {
                    if (worker != null && worker.CancellationPending) {
                        client.CancelAsync();
                    }
                }
                if (cancelled || (worker != null && worker.CancellationPending)) {
                    if (File.Exists(destination)) {
                        File.Delete(destination);
                    }
                    throw new OperationCanceledException("The update was cancelled.");
                }
                if (failure != null) {
                    throw new InvalidOperationException("The update download failed.", failure);
                }
            }
        }

        private static WebClient CreateWebClient() {
            WebClient client = new WebClient();
            client.Headers[HttpRequestHeader.UserAgent] = "HyruleCoopLauncher/1.1";
            return client;
        }

        private static bool IsAllowedDownloadUri(Uri uri) {
            return uri.IsFile || String.Equals(uri.Scheme, Uri.UriSchemeHttps, StringComparison.OrdinalIgnoreCase) ||
                   (String.Equals(uri.Scheme, Uri.UriSchemeHttp, StringComparison.OrdinalIgnoreCase) && uri.IsLoopback);
        }

        private static void VerifyHash(string path, string expected, string label) {
            string actual = Hashing.Sha256(path);
            if (!String.Equals(actual, expected, StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidDataException("The " + label + " failed SHA-256 verification.");
            }
        }

        private static void ExtractArchiveSafely(string archivePath, string destination) {
            string root = Path.GetFullPath(destination) + Path.DirectorySeparatorChar;
            using (ZipArchive archive = ZipFile.OpenRead(archivePath)) {
                foreach (ZipArchiveEntry entry in archive.Entries) {
                    string normalized = entry.FullName.Replace('/', Path.DirectorySeparatorChar);
                    string target = Path.GetFullPath(Path.Combine(destination, normalized));
                    if (!target.StartsWith(root, StringComparison.OrdinalIgnoreCase)) {
                        throw new InvalidDataException("The runtime archive contains an unsafe path.");
                    }
                    if (String.IsNullOrEmpty(entry.Name)) {
                        Directory.CreateDirectory(target);
                        continue;
                    }
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    using (Stream source = entry.Open())
                    using (FileStream output = new FileStream(target, FileMode.Create, FileAccess.Write, FileShare.None)) {
                        source.CopyTo(output);
                    }
                }
            }
        }

        private string ResolveBootstrapArchive(ReleaseManifest manifest) {
            if (String.IsNullOrWhiteSpace(manifest.runtimeArchive) || String.IsNullOrWhiteSpace(bootstrapPayloadRoot)) {
                return null;
            }
            string bootstrapDirectory = Path.GetFullPath(Path.Combine(bootstrapPayloadRoot, "_bootstrap"));
            string archive = Path.GetFullPath(Path.Combine(bootstrapDirectory, manifest.runtimeArchive));
            if (!archive.StartsWith(bootstrapDirectory + Path.DirectorySeparatorChar,
                                    StringComparison.OrdinalIgnoreCase) || !File.Exists(archive)) {
                throw new FileNotFoundException("The bundled runtime archive is missing.", archive);
            }
            return archive;
        }

        private string RuntimeDirectory(string releaseId) {
            if (!SafeReleaseId.IsMatch(releaseId ?? "")) {
                throw new InvalidDataException("Invalid runtime release identifier.");
            }
            return Path.Combine(runtimeRoot, releaseId);
        }

        private string ReadReleasePointer(string name) {
            string path = Path.Combine(rootPath, name);
            if (!File.Exists(path)) {
                return null;
            }
            string releaseId = File.ReadAllText(path).Trim();
            return SafeReleaseId.IsMatch(releaseId) ? releaseId : null;
        }

        private List<ExistingInstallation> DiscoverExistingInstallations() {
            List<ExistingInstallation> candidates = new List<ExistingInstallation>();
            HashSet<string> visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (String.IsNullOrWhiteSpace(options.BootstrapRoot)) {
                return candidates;
            }

            TryAddCandidate(options.BootstrapRoot, candidates, visited);
            DirectoryInfo parent = Directory.GetParent(options.BootstrapRoot);
            if (parent == null) {
                return candidates;
            }
            foreach (DirectoryInfo sibling in parent.GetDirectories()) {
                if ((sibling.Attributes & FileAttributes.ReparsePoint) != 0) {
                    continue;
                }
                TryAddCandidate(sibling.FullName, candidates, visited);
            }
            return candidates;
        }

        private void TryAddCandidate(string path, List<ExistingInstallation> candidates, HashSet<string> visited) {
            string fullPath;
            try {
                fullPath = Path.GetFullPath(path);
            } catch {
                return;
            }
            if (!visited.Add(fullPath)) {
                return;
            }
            ExistingInstallation candidate = BuildCandidate(fullPath);
            if (candidate != null) {
                candidates.Add(candidate);
            }
        }

        private ExistingInstallation BuildCandidate(string path) {
            string fullPath = Path.GetFullPath(path);
            if (!File.Exists(Path.Combine(fullPath, "soh.exe"))) {
                return null;
            }
            string o2r = FirstExisting(Path.Combine(fullPath, "oot.o2r"), Path.Combine(fullPath, "oot-mq.o2r"));
            bool hasSentinel = !String.IsNullOrWhiteSpace(o2r) ||
                               File.Exists(Path.Combine(fullPath, "Save", "global.sav")) ||
                               File.Exists(Path.Combine(fullPath, "logs", "Ship of Harkinian.log"));
            if (!hasSentinel) {
                return null;
            }

            ExistingInstallation candidate = new ExistingInstallation();
            candidate.Path = fullPath;
            candidate.O2rPath = o2r;
            if (!String.IsNullOrWhiteSpace(o2r)) {
                candidate.O2rHash = Hashing.Sha256(o2r);
            }
            return candidate;
        }

        private void ImportCandidate(ExistingInstallation candidate, IWin32Window owner, bool headless) {
            Log("Importing player data from " + candidate.Path);
            CopyIfMissing(Path.Combine(candidate.Path, "oot.o2r"), Path.Combine(userDataRoot, "oot.o2r"));
            CopyIfMissing(Path.Combine(candidate.Path, "oot-mq.o2r"), Path.Combine(userDataRoot, "oot-mq.o2r"));
            ImportPreferencesIfSafe(Path.Combine(candidate.Path, "shipofharkinian.json"), candidate.Path);
            CopyDirectoryIfMissing(Path.Combine(candidate.Path, "mods"), Path.Combine(userDataRoot, "mods"));
            CopyDirectoryIfMissing(Path.Combine(candidate.Path, "Save"), Path.Combine(rootPath, "Save"));

            bool hasGeneratedArchive = File.Exists(Path.Combine(userDataRoot, "oot.o2r")) ||
                                       File.Exists(Path.Combine(userDataRoot, "oot-mq.o2r"));
            string[] roms = FindTopLevelRoms(candidate.Path);
            if (!headless && roms.Length > 0) {
                DialogResult copy = MessageBox.Show(owner,
                    "A ROM already appears on this device:\n\n" + roms[0] +
                    "\n\nCopy it into Hyrule Co-op's AppData folder for future asset regeneration? " +
                     "The original will not be changed.",
                    "Reuse existing ROM", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
                if (copy == DialogResult.Yes) {
                    ImportRomFile(roms[0], true);
                } else if (!hasGeneratedArchive) {
                    ImportRomFile(roms[0], false);
                }
            }
            if (hasGeneratedArchive || !String.IsNullOrWhiteSpace(pendingRomPath)) {
                WriteMigrationMarker(candidate.Path);
            } else {
                Log("Imported player data, but asset setup remains incomplete because no O2R or ROM was selected.");
            }
        }

        private void ImportRom(string path, IWin32Window owner) {
            DialogResult copy = MessageBox.Show(owner,
                "Copy this ROM into Hyrule Co-op's AppData folder?\n\n" + path +
                "\n\nChoose No to use it from its current location for this extraction.",
                "ROM storage", MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            ImportRomFile(path, copy == DialogResult.Yes);
        }

        private void ImportRomFile(string source, bool copy) {
            if (!File.Exists(source) || !IsRomPath(source)) {
                throw new InvalidDataException("The selected file is not a supported N64 ROM file.");
            }
            string selected = Path.GetFullPath(source);
            if (copy) {
                string romDirectory = Path.Combine(userDataRoot, "ROMs");
                Directory.CreateDirectory(romDirectory);
                string destination = Path.Combine(romDirectory, Path.GetFileName(source));
                if (!File.Exists(destination) || !String.Equals(Hashing.Sha256(source), Hashing.Sha256(destination),
                                                                 StringComparison.OrdinalIgnoreCase)) {
                    File.Copy(source, destination, true);
                }
                selected = destination;
            }
            pendingRomPath = selected;
            AtomicFile.WriteAllText(Path.Combine(rootPath, "pending-rom.txt"), selected);
        }

        private string UpdateAssetMetadata(ReleaseManifest manifest, string runtimeDirectory) {
            string metadataPath = Path.Combine(userDataRoot, "asset-metadata.json");
            Dictionary<string, object> previous = ReadMetadata(metadataPath);
            string o2rPath = FirstExisting(Path.Combine(userDataRoot, "oot.o2r"),
                                           Path.Combine(userDataRoot, "oot-mq.o2r"));
            string currentO2rHash = File.Exists(o2rPath) ? Hashing.Sha256(o2rPath) : null;
            string previousO2rHash = NestedString(previous, "oot", "sha256") ??
                                     NestedString(previous, "ootMq", "sha256");
            int generatedAssetSchema = manifest.assetSchema;
            if (!String.IsNullOrWhiteSpace(currentO2rHash) &&
                String.Equals(currentO2rHash, previousO2rHash, StringComparison.OrdinalIgnoreCase)) {
                generatedAssetSchema = DictionaryInt(previous, "generatedAssetSchema",
                                                     DictionaryInt(previous, "assetSchema", manifest.assetSchema));
            }
            bool regenerationRequired = !String.IsNullOrWhiteSpace(currentO2rHash) &&
                                        generatedAssetSchema < manifest.assetSchema;

            Dictionary<string, object> metadata = new Dictionary<string, object>();
            metadata["schemaVersion"] = 1;
            metadata["assetSchema"] = manifest.assetSchema;
            metadata["generatedAssetSchema"] = generatedAssetSchema;
            metadata["regenerationRequired"] = regenerationRequired;
            metadata["releaseId"] = manifest.releaseId;
            metadata["updatedUtc"] = DateTime.UtcNow.ToString("o");
            AddFileMetadata(metadata, "oot", Path.Combine(userDataRoot, "oot.o2r"));
            AddFileMetadata(metadata, "ootMq", Path.Combine(userDataRoot, "oot-mq.o2r"));
            AddFileMetadata(metadata, "extractor", Path.Combine(runtimeDirectory, "extractor-assets.zip"));
            string[] roms = FindTopLevelRoms(Path.Combine(userDataRoot, "ROMs"));
            if (roms.Length > 0) {
                AddFileMetadata(metadata, "sourceRom", roms[0]);
            }
            AtomicFile.WriteAllText(metadataPath, serializer.Serialize(metadata));
            if (!regenerationRequired) {
                return null;
            }

            string warning = "This Hyrule Co-op update uses a newer generated-asset schema. Your existing O2R " +
                             "was preserved, but it must be regenerated from your ROM before this runtime is " +
                             "considered fully compatible. See the launcher log for the retained asset metadata.";
            Log(warning);
            return warning;
        }

        private Dictionary<string, object> ReadMetadata(string path) {
            if (!File.Exists(path)) {
                return null;
            }
            try {
                return serializer.Deserialize<Dictionary<string, object>>(File.ReadAllText(path));
            } catch (Exception ex) {
                Log("Ignoring invalid asset metadata: " + ex.Message);
                return null;
            }
        }

        private static string NestedString(Dictionary<string, object> values, string name, string childName) {
            if (values == null || !values.ContainsKey(name)) {
                return null;
            }
            Dictionary<string, object> child = values[name] as Dictionary<string, object>;
            if (child == null || !child.ContainsKey(childName)) {
                return null;
            }
            return Convert.ToString(child[childName]);
        }

        private static int DictionaryInt(Dictionary<string, object> values, string name, int fallback) {
            if (values == null || !values.ContainsKey(name)) {
                return fallback;
            }
            try {
                return Convert.ToInt32(values[name]);
            } catch {
                return fallback;
            }
        }

        private static void AddFileMetadata(Dictionary<string, object> metadata, string name, string path) {
            if (!File.Exists(path)) {
                return;
            }
            Dictionary<string, object> file = new Dictionary<string, object>();
            file["fileName"] = Path.GetFileName(path);
            file["sha256"] = Hashing.Sha256(path);
            file["size"] = new FileInfo(path).Length;
            metadata[name] = file;
        }

        private void WriteMigrationMarker(string source) {
            Dictionary<string, object> marker = new Dictionary<string, object>();
            marker["schemaVersion"] = 1;
            marker["completedUtc"] = DateTime.UtcNow.ToString("o");
            marker["source"] = source;
            AtomicFile.WriteAllText(Path.Combine(rootPath, "migration-v1.json"), serializer.Serialize(marker));
        }

        private void MigratePreferences(List<ExistingInstallation> candidates) {
            string markerPath = Path.Combine(rootPath, "preferences-migration-v1.json");
            if (File.Exists(markerPath)) {
                return;
            }

            ExistingInstallation newest = null;
            DateTime newestWriteTime = DateTime.MinValue;
            foreach (ExistingInstallation candidate in candidates) {
                string source = Path.Combine(candidate.Path, "shipofharkinian.json");
                if (!File.Exists(source)) {
                    continue;
                }
                DateTime writeTime = File.GetLastWriteTimeUtc(source);
                if (newest == null || writeTime > newestWriteTime) {
                    newest = candidate;
                    newestWriteTime = writeTime;
                }
            }

            string destination = Path.Combine(userDataRoot, "shipofharkinian.json");
            if (newest != null && ImportPreferencesIfSafe(
                    Path.Combine(newest.Path, "shipofharkinian.json"), newest.Path)) {
                return;
            }
            if (File.Exists(destination) && !IsBootstrapDefaultConfiguration(destination)) {
                WritePreferenceMigrationMarker("existing AppData preferences", "preserved");
            }
        }

        private bool ImportPreferencesIfSafe(string source, string sourceRoot) {
            if (!File.Exists(source)) {
                return false;
            }

            string destination = Path.Combine(userDataRoot, "shipofharkinian.json");
            if (File.Exists(destination) && !IsBootstrapDefaultConfiguration(destination)) {
                WritePreferenceMigrationMarker("existing AppData preferences", "preserved");
                return false;
            }

            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            File.Copy(source, destination, true);
            WritePreferenceMigrationMarker(sourceRoot, "imported");
            Log("Imported player preferences from " + source);
            return true;
        }

        private bool IsBootstrapDefaultConfiguration(string path) {
            if (!File.Exists(path)) {
                return false;
            }
            string defaultContents = ReadBootstrapDefaultConfiguration();
            return defaultContents != null &&
                   String.Equals(File.ReadAllText(path), defaultContents, StringComparison.Ordinal);
        }

        private string ReadBootstrapDefaultConfiguration() {
            if (!String.IsNullOrWhiteSpace(bootstrapPayloadRoot)) {
                string path = Path.Combine(bootstrapPayloadRoot, "_bootstrap", "default-shipofharkinian.json");
                if (File.Exists(path)) {
                    return File.ReadAllText(path);
                }
            }
            using (Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("HyruleCoop.DefaultConfig")) {
                if (stream == null) {
                    return null;
                }
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8, true)) {
                    return reader.ReadToEnd();
                }
            }
        }

        private void WritePreferenceMigrationMarker(string source, string action) {
            Dictionary<string, object> marker = new Dictionary<string, object>();
            marker["schemaVersion"] = 1;
            marker["completedUtc"] = DateTime.UtcNow.ToString("o");
            marker["source"] = source;
            marker["action"] = action;
            AtomicFile.WriteAllText(Path.Combine(rootPath, "preferences-migration-v1.json"),
                                    serializer.Serialize(marker));
        }

        private static void CopyIfMissing(string source, string destination) {
            if (!File.Exists(source) || File.Exists(destination)) {
                return;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            File.Copy(source, destination, false);
        }

        private static void CopyDirectoryIfMissing(string source, string destination) {
            if (!Directory.Exists(source)) {
                return;
            }
            DirectoryInfo sourceInfo = new DirectoryInfo(source);
            if ((sourceInfo.Attributes & FileAttributes.ReparsePoint) != 0) {
                return;
            }
            Directory.CreateDirectory(destination);
            foreach (FileInfo file in sourceInfo.GetFiles()) {
                string target = Path.Combine(destination, file.Name);
                if (!File.Exists(target)) {
                    file.CopyTo(target, false);
                }
            }
            foreach (DirectoryInfo child in sourceInfo.GetDirectories()) {
                if ((child.Attributes & FileAttributes.ReparsePoint) == 0) {
                    CopyDirectoryIfMissing(child.FullName, Path.Combine(destination, child.Name));
                }
            }
        }

        private static string[] FindTopLevelRoms(string directory) {
            if (!Directory.Exists(directory)) {
                return new string[0];
            }
            List<string> roms = new List<string>();
            foreach (string file in Directory.GetFiles(directory)) {
                if (IsRomPath(file)) {
                    roms.Add(file);
                }
            }
            return roms.ToArray();
        }

        private static bool IsRomPath(string path) {
            string extension = Path.GetExtension(path).ToLowerInvariant();
            return extension == ".z64" || extension == ".n64" || extension == ".v64";
        }

        private static string FirstExisting(params string[] paths) {
            foreach (string path in paths) {
                if (File.Exists(path)) {
                    return path;
                }
            }
            return null;
        }

        private static bool IsSafeRelativePath(string path) {
            if (String.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path)) {
                return false;
            }
            string normalized = path.Replace('\\', '/');
            return !normalized.StartsWith("/", StringComparison.Ordinal) &&
                   !normalized.Contains("../") && normalized != ".." && normalized.IndexOf(':') < 0;
        }

        private static bool IsPlayerOwnedPath(string path) {
            string normalized = path.Replace('\\', '/').TrimStart('/').ToLowerInvariant();
            string first = normalized.Split('/')[0];
            return first == "save" || first == "mods" || first == "logs" || first == "userdata" ||
                   first == "shipofharkinian.json" || first == "imgui.ini" || first == "oot.o2r" ||
                   first == "oot-mq.o2r" ||
                   normalized.EndsWith(".z64") || normalized.EndsWith(".n64") || normalized.EndsWith(".v64") ||
                   normalized.EndsWith(".sav");
        }

        private static int CompareVersions(string left, string right) {
            Version leftVersion;
            Version rightVersion;
            if (Version.TryParse(left, out leftVersion) && Version.TryParse(right, out rightVersion)) {
                return leftVersion.CompareTo(rightVersion);
            }
            return String.Compare(left ?? "", right ?? "", StringComparison.OrdinalIgnoreCase);
        }

        private static string JoinWarnings(string first, string second) {
            if (String.IsNullOrWhiteSpace(first)) {
                return second;
            }
            if (String.IsNullOrWhiteSpace(second)) {
                return first;
            }
            return first + "\n\n" + second;
        }

        private static string FormatBytes(long bytes) {
            if (bytes < 0) {
                return "unknown";
            }
            if (bytes >= 1024L * 1024L) {
                return (bytes / (1024.0 * 1024.0)).ToString("0.0") + " MB";
            }
            return (bytes / 1024.0).ToString("0.0") + " KB";
        }

        private static void SafeDeleteDirectory(string path, string allowedRoot) {
            string fullPath = Path.GetFullPath(path);
            string fullRoot = Path.GetFullPath(allowedRoot) + Path.DirectorySeparatorChar;
            if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException("Refusing to remove a directory outside the runtime root.");
            }
            Directory.Delete(fullPath, true);
        }

        private void Report(BackgroundWorker worker, string message, int percent, bool indeterminate) {
            Log(message);
            if (worker != null) {
                ProgressUpdate update = new ProgressUpdate();
                update.Message = message;
                update.Percent = percent;
                update.Indeterminate = indeterminate;
                worker.ReportProgress(percent, update);
            }
        }

        private void Log(string message) {
            try {
                Directory.CreateDirectory(Path.GetDirectoryName(logPath));
                File.AppendAllText(logPath, DateTime.UtcNow.ToString("o") + " " + message + Environment.NewLine,
                                   new UTF8Encoding(false));
            } catch {
            }
        }
    }

    internal sealed class SetupDialog : Form {
        private readonly ComboBox candidateList;
        private readonly Button importButton;
        public ExistingInstallation SelectedInstallation { get; private set; }
        public string SelectedRomPath { get; private set; }

        public SetupDialog(List<ExistingInstallation> candidates) {
            Text = "Set up Hyrule Co-op";
            ClientSize = new Size(620, 250);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterParent;

            Label heading = new Label();
            heading.AutoSize = true;
            heading.Font = new Font(Font.FontFamily, 13.0f, FontStyle.Bold);
            heading.Location = new Point(22, 20);
            heading.Text = "Reuse your existing Shipwright files";
            Controls.Add(heading);

            Label explanation = new Label();
            explanation.Location = new Point(24, 55);
            explanation.Size = new Size(570, 48);
            explanation.Text = "Only immediate sibling installations were checked. The selected source is copied " +
                               "without changing or deleting its files.";
            Controls.Add(explanation);

            candidateList = new ComboBox();
            candidateList.DropDownStyle = ComboBoxStyle.DropDownList;
            candidateList.Location = new Point(27, 105);
            candidateList.Size = new Size(565, 24);
            foreach (ExistingInstallation candidate in candidates) {
                candidateList.Items.Add(candidate);
            }
            if (candidateList.Items.Count > 0) {
                candidateList.SelectedIndex = 0;
            }
            Controls.Add(candidateList);

            importButton = new Button();
            importButton.Location = new Point(27, 158);
            importButton.Size = new Size(155, 32);
            importButton.Text = "Import selected";
            importButton.Enabled = candidateList.Items.Count > 0;
            importButton.Click += ImportSelected;
            Controls.Add(importButton);

            Button chooseInstall = new Button();
            chooseInstall.Location = new Point(190, 158);
            chooseInstall.Size = new Size(155, 32);
            chooseInstall.Text = "Choose installation";
            chooseInstall.Click += ChooseInstallation;
            Controls.Add(chooseInstall);

            Button chooseRom = new Button();
            chooseRom.Location = new Point(353, 158);
            chooseRom.Size = new Size(115, 32);
            chooseRom.Text = "Choose ROM";
            chooseRom.Click += ChooseRom;
            Controls.Add(chooseRom);

            Button skip = new Button();
            skip.Location = new Point(476, 158);
            skip.Size = new Size(115, 32);
            skip.Text = "Skip";
            skip.DialogResult = DialogResult.Cancel;
            Controls.Add(skip);
            CancelButton = skip;
        }

        private void ImportSelected(object sender, EventArgs e) {
            SelectedInstallation = candidateList.SelectedItem as ExistingInstallation;
            if (SelectedInstallation != null) {
                DialogResult = DialogResult.OK;
                Close();
            }
        }

        private void ChooseInstallation(object sender, EventArgs e) {
            using (FolderBrowserDialog browser = new FolderBrowserDialog()) {
                browser.Description = "Choose a Shipwright installation containing soh.exe.";
                if (browser.ShowDialog(this) != DialogResult.OK) {
                    return;
                }
                ExistingInstallation candidate = BuildCandidateForDialog(browser.SelectedPath);
                if (candidate == null) {
                    MessageBox.Show(this, "That folder is not a recognized Shipwright installation.", "Hyrule Co-op",
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                SelectedInstallation = candidate;
                DialogResult = DialogResult.OK;
                Close();
            }
        }

        private static ExistingInstallation BuildCandidateForDialog(string path) {
            string fullPath = Path.GetFullPath(path);
            if (!File.Exists(Path.Combine(fullPath, "soh.exe"))) {
                return null;
            }
            string o2r = File.Exists(Path.Combine(fullPath, "oot.o2r")) ? Path.Combine(fullPath, "oot.o2r") :
                         (File.Exists(Path.Combine(fullPath, "oot-mq.o2r")) ? Path.Combine(fullPath, "oot-mq.o2r") : null);
            if (o2r == null && !File.Exists(Path.Combine(fullPath, "Save", "global.sav")) &&
                !File.Exists(Path.Combine(fullPath, "logs", "Ship of Harkinian.log"))) {
                return null;
            }
            ExistingInstallation candidate = new ExistingInstallation();
            candidate.Path = fullPath;
            candidate.O2rPath = o2r;
            candidate.O2rHash = o2r == null ? null : Hashing.Sha256(o2r);
            return candidate;
        }

        private void ChooseRom(object sender, EventArgs e) {
            using (OpenFileDialog browser = new OpenFileDialog()) {
                browser.Title = "Choose your Ocarina of Time ROM";
                browser.Filter = "Nintendo 64 ROM (*.z64;*.n64;*.v64)|*.z64;*.n64;*.v64|All files (*.*)|*.*";
                if (browser.ShowDialog(this) == DialogResult.OK) {
                    SelectedRomPath = browser.FileName;
                    DialogResult = DialogResult.Yes;
                    Close();
                }
            }
        }
    }
}
