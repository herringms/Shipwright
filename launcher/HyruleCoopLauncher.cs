using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Windows.Forms;

[assembly: AssemblyTitle("Hyrule Co-op Launcher")]
[assembly: AssemblyDescription("Updates and launches Hyrule Co-op")]
[assembly: AssemblyCompany("Hyrule Co-op contributors")]
[assembly: AssemblyProduct("Hyrule Co-op")]
[assembly: AssemblyVersion("1.1.1.0")]
[assembly: AssemblyFileVersion("1.1.1.0")]

namespace HyruleCoop.Launcher {
    internal sealed class LauncherOptions {
        public string RootPath;
        public string BootstrapRoot;
        public string BootstrapPayloadRoot;
        public string ManifestOverride;
        public string ImportCandidate;
        public bool Delegated;
        public bool Headless;
        public bool NoLaunch;
        public bool Offline;
        public bool Rollback;
        public bool SkipImport;
        public string[] OriginalArguments;

        public static LauncherOptions Parse(string[] args) {
            LauncherOptions options = new LauncherOptions();
            options.OriginalArguments = args;
            options.RootPath = Environment.GetEnvironmentVariable("HYRULE_COOP_ROOT");

            for (int i = 0; i < args.Length; i++) {
                string value = args[i];
                if (value == "--root" && i + 1 < args.Length) {
                    options.RootPath = args[++i];
                } else if (value == "--bootstrap-root" && i + 1 < args.Length) {
                    options.BootstrapRoot = args[++i];
                } else if (value == "--bootstrap-payload-root" && i + 1 < args.Length) {
                    options.BootstrapPayloadRoot = args[++i];
                } else if (value == "--manifest" && i + 1 < args.Length) {
                    options.ManifestOverride = args[++i];
                } else if (value == "--import-candidate" && i + 1 < args.Length) {
                    options.ImportCandidate = args[++i];
                } else if (value == "--delegated") {
                    options.Delegated = true;
                } else if (value == "--headless") {
                    options.Headless = true;
                } else if (value == "--no-launch") {
                    options.NoLaunch = true;
                } else if (value == "--offline") {
                    options.Offline = true;
                } else if (value == "--rollback") {
                    options.Rollback = true;
                } else if (value == "--skip-import") {
                    options.SkipImport = true;
                }
            }

            if (String.IsNullOrWhiteSpace(options.RootPath)) {
                options.RootPath = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "HyruleCoop");
            }
            options.RootPath = Path.GetFullPath(options.RootPath);
            if (!String.IsNullOrWhiteSpace(options.BootstrapRoot)) {
                options.BootstrapRoot = Path.GetFullPath(options.BootstrapRoot);
            }
            if (!String.IsNullOrWhiteSpace(options.BootstrapPayloadRoot)) {
                options.BootstrapPayloadRoot = Path.GetFullPath(options.BootstrapPayloadRoot);
            }
            return options;
        }

        public string ForwardedArguments(bool delegated) {
            List<string> forwarded = new List<string>();
            for (int i = 0; i < OriginalArguments.Length; i++) {
                if (OriginalArguments[i] == "--delegated") {
                    continue;
                }
                forwarded.Add(OriginalArguments[i]);
            }
            if (delegated) {
                forwarded.Add("--delegated");
            }
            return ArgumentQuoting.Join(forwarded);
        }
    }

    internal static class ArgumentQuoting {
        public static string Join(IEnumerable<string> arguments) {
            List<string> quoted = new List<string>();
            foreach (string argument in arguments) {
                quoted.Add(Quote(argument));
            }
            return String.Join(" ", quoted.ToArray());
        }

        public static string Quote(string value) {
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

    internal static class Program {
        public const string LauncherVersion = "1.1.1";

        [STAThread]
        private static int Main(string[] args) {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            LauncherOptions options = LauncherOptions.Parse(args);

            try {
                Directory.CreateDirectory(options.RootPath);
                if (!options.Delegated && DelegateToInstalledLauncher(options)) {
                    return 0;
                }

                using (Mutex mutex = new Mutex(false, "Local\\HyruleCoopLauncher-" + SafeMutexSuffix(options.RootPath))) {
                    if (!mutex.WaitOne(0, false)) {
                        if (!options.Headless) {
                            MessageBox.Show("Hyrule Co-op is already starting.", "Hyrule Co-op", MessageBoxButtons.OK,
                                            MessageBoxIcon.Information);
                        }
                        return 0;
                    }

                    LauncherEngine engine = new LauncherEngine(options, LauncherVersion);
                    if (options.Headless) {
                        return engine.RunHeadless();
                    }

                    Application.Run(new LauncherForm(engine));
                    return engine.ExitCode;
                }
            } catch (Exception ex) {
                if (options.Headless) {
                    return 1;
                }
                MessageBox.Show(ex.Message, "Hyrule Co-op launcher error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        private static bool DelegateToInstalledLauncher(LauncherOptions options) {
            string currentExecutable = Path.GetFullPath(Assembly.GetExecutingAssembly().Location);
            string installDirectory = Path.Combine(options.RootPath, "Launcher", LauncherVersion);
            string installedExecutable = Path.Combine(installDirectory, "HyruleCoopLauncher.exe");
            Directory.CreateDirectory(installDirectory);

            if (!File.Exists(installedExecutable) ||
                !String.Equals(Hashing.Sha256(currentExecutable), Hashing.Sha256(installedExecutable),
                               StringComparison.OrdinalIgnoreCase)) {
                File.Copy(currentExecutable, installedExecutable, true);
            }
            AtomicFile.WriteAllText(Path.Combine(options.RootPath, "Launcher", "current.txt"), installedExecutable);

            if (String.Equals(currentExecutable, Path.GetFullPath(installedExecutable),
                              StringComparison.OrdinalIgnoreCase)) {
                return false;
            }

            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = installedExecutable;
            startInfo.Arguments = options.ForwardedArguments(true);
            startInfo.WorkingDirectory = installDirectory;
            startInfo.UseShellExecute = false;
            Process.Start(startInfo);
            return true;
        }

        private static string SafeMutexSuffix(string value) {
            return unchecked((uint)value.ToUpperInvariant().GetHashCode()).ToString("X8");
        }
    }

    internal sealed class LauncherForm : Form {
        private readonly LauncherEngine engine;
        private readonly Label titleLabel;
        private readonly Label statusLabel;
        private readonly ProgressBar progressBar;
        private readonly Button cancelButton;
        private readonly BackgroundWorker worker;

        public LauncherForm(LauncherEngine launcherEngine) {
            engine = launcherEngine;
            Text = "Hyrule Co-op";
            ClientSize = new Size(560, 190);
            MinimumSize = new Size(520, 220);
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = true;
            ShowInTaskbar = true;

            titleLabel = new Label();
            titleLabel.AutoSize = true;
            titleLabel.Font = new Font(Font.FontFamily, 16.0f, FontStyle.Bold);
            titleLabel.Location = new Point(24, 22);
            titleLabel.Text = "Hyrule Co-op";
            Controls.Add(titleLabel);

            statusLabel = new Label();
            statusLabel.AutoEllipsis = true;
            statusLabel.Location = new Point(27, 67);
            statusLabel.Size = new Size(505, 42);
            statusLabel.Text = "Preparing launcher...";
            Controls.Add(statusLabel);

            progressBar = new ProgressBar();
            progressBar.Location = new Point(28, 115);
            progressBar.Size = new Size(400, 24);
            progressBar.Style = ProgressBarStyle.Marquee;
            Controls.Add(progressBar);

            cancelButton = new Button();
            cancelButton.Location = new Point(444, 113);
            cancelButton.Size = new Size(88, 28);
            cancelButton.Text = "Cancel";
            cancelButton.Click += delegate { worker.CancelAsync(); cancelButton.Enabled = false; };
            Controls.Add(cancelButton);

            worker = new BackgroundWorker();
            worker.WorkerReportsProgress = true;
            worker.WorkerSupportsCancellation = true;
            worker.DoWork += WorkerDoWork;
            worker.ProgressChanged += WorkerProgressChanged;
            worker.RunWorkerCompleted += WorkerCompleted;
        }

        protected override void OnShown(EventArgs e) {
            base.OnShown(e);
            try {
                engine.RunFirstSetup(this);
                worker.RunWorkerAsync();
            } catch (Exception ex) {
                engine.RecordFailure(ex);
                MessageBox.Show(this, ex.Message, "Hyrule Co-op setup error", MessageBoxButtons.OK,
                                MessageBoxIcon.Error);
                Close();
            }
        }

        protected override void OnFormClosing(FormClosingEventArgs e) {
            if (worker.IsBusy && !worker.CancellationPending) {
                worker.CancelAsync();
            }
            base.OnFormClosing(e);
        }

        private void WorkerDoWork(object sender, DoWorkEventArgs e) {
            e.Result = engine.Run((BackgroundWorker)sender);
        }

        private void WorkerProgressChanged(object sender, ProgressChangedEventArgs e) {
            ProgressUpdate update = e.UserState as ProgressUpdate;
            if (update == null) {
                return;
            }
            statusLabel.Text = update.Message;
            if (update.Indeterminate) {
                progressBar.Style = ProgressBarStyle.Marquee;
            } else {
                progressBar.Style = ProgressBarStyle.Continuous;
                progressBar.Value = Math.Max(0, Math.Min(100, update.Percent));
            }
        }

        private void WorkerCompleted(object sender, RunWorkerCompletedEventArgs e) {
            cancelButton.Enabled = false;
            if (e.Error != null) {
                engine.RecordFailure(e.Error);
                MessageBox.Show(this, e.Error.Message, "Hyrule Co-op could not start", MessageBoxButtons.OK,
                                MessageBoxIcon.Error);
                Close();
                return;
            }

            LaunchOutcome outcome = e.Result as LaunchOutcome;
            if (outcome != null && outcome.RelaunchLauncher) {
                engine.StartReplacementLauncher(outcome.ReplacementLauncherPath);
            } else if (outcome != null && outcome.GameExecutable != null) {
                if (!String.IsNullOrWhiteSpace(outcome.WarningMessage)) {
                    MessageBox.Show(this, outcome.WarningMessage, "Hyrule Co-op assets",
                                    MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
                engine.StartGame(outcome);
            }
            Close();
        }
    }
}
