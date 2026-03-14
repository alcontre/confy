# confy

**confy** is a cross-platform desktop application that lets you define a set of software components in a single XML configuration file and then download all of them -- source code from Bitbucket and/or pre-built artifacts from Nexus -- with a few clicks.

---

## What does confy do?

Large projects often consist of many repositories and binary artifacts that need to be checked out or downloaded together. **confy** solves this by:

1. Reading an XML *config file* that lists every component, where it lives, and how to fetch it.
2. Presenting a simple checklist UI so you can choose exactly which components (and which branch/version) to download.
3. Downloading everything in parallel to a local directory while keeping the UI responsive.
4. Running optional post-download scripts for each component.

---

## Prerequisites

| Requirement | Notes |
|---|---|
| **vcpkg** | Required for non-vendored C/C++ dependencies such as `curl` |
| **git** | Must be on your `PATH`; used for all source checkouts |
| **Network access** | To your Bitbucket Data Center and/or Nexus repository |
| **Credentials** | Bitbucket HTTPS credentials read from `~/.m2/settings.xml` |

`wxWidgets`, `doctest`, `nlohmann_json`, and `rapidxml` remain vendored in this repository. `curl` is resolved via the checked-in vcpkg manifest.

---

## Getting started

### Build from source

```bash
# One-time setup: install vcpkg locally
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
"$HOME/vcpkg/bootstrap-vcpkg.sh"
export VCPKG_ROOT="$HOME/vcpkg"

# Configure and build with the vcpkg toolchain
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build -j

# Or use the one-stop clean-rebuild script (Linux / macOS)
./build.sh --vcpkg
```

On the first configure, vcpkg manifest mode installs `curl` into the local `vcpkg_installed/` directory automatically.

`./build.sh` now uses the default CMake dependency resolution path. Pass `--vcpkg` to make it opt into the vcpkg toolchain.

Windows developers can use `./winbuild2019.ps1`, which already locates the vcpkg toolchain and configures the project with it.

Then run the app:

```bash
./build/confy
```

---

## Using confy

### 1. Open a config file

Launch confy and choose **File -> Load Config...** to open an XML config file (see [Config file format](#config-file-format) below). The app remembers the last loaded file across sessions.

### 2. Review the component list

The main window shows a checklist of all components defined in the config. Each component can have:

- a **Source** section -- clones a Git repository
- an **Artifact** section -- downloads binaries from a Nexus raw repository
- or **both**

Use the checkboxes to select only the components you want to download.

### 3. Choose branches / versions

For each enabled Source entry, use the branch/tag dropdown to pick the exact ref to clone. For Artifact entries, choose the version and build type from the dropdowns.

### 4. Apply

Click **Apply** to start downloading. A progress dialog shows live status for every component. Network requests run in the background so the UI remains responsive. Progress and any errors are also visible in **View -> Debug Console**.

---

## Config file format

The config file is an XML document that describes where your components live and how to fetch them.

```xml
<Config>
    <version>9</version>
    <!-- Root directory on your machine where everything will be downloaded -->
    <path>/path/to/local/storage</path>
    <components>
        <Component>
            <name>my_component</name>
            <DisplayName>My Component</DisplayName>
            <!-- Subdirectory under <path> where this component is placed -->
            <Path>my_component</Path>
            <!-- You can also use %PATH% to inject the top-level <path> value -->
            <!-- Example: <Path>%PATH%/my_component</Path> -->

            <!-- Source: clone from a Git / Bitbucket repository -->
            <Source>
                <IsEnabled/>   <!-- remove this tag to disable by default -->
                <url>https://bitbucket.example.com/project/myrepo.git</url>
                <BranchOrTag>main</BranchOrTag>
                <!-- Add <NoShallow/> to perform a full (non-shallow) clone -->
                <Script>post_source.sh</Script>   <!-- optional post-clone script -->
            </Source>

            <!-- Artifact: download binaries from a Nexus raw repository -->
            <Artifact>
                <IsEnabled/>   <!-- remove this tag to disable by default -->
                <url>https://nexus.example.com/#browse/browse:my-raw-repo</url>
                <version>1.2.3</version>
                <buildtype>Release</buildtype>
                <!-- Optional file filters (regular expressions) -->
                <regex-include>
                    <regex>\.dll$</regex>
                </regex-include>
                <regex-exclude>
                    <regex>/tests?/</regex>
                </regex-exclude>
                <script>post_artifact.sh</script>   <!-- optional post-download script -->
            </Artifact>
        </Component>

        <!-- A component can have only Source, only Artifact, or both -->
        <Component>
            <name>source_only</name>
            <DisplayName>Source Only Component</DisplayName>
            <Path>source_only</Path>
            <Source>
                <IsEnabled/>
                <url>https://bitbucket.example.com/project/other.git</url>
                <BranchOrTag>release/2.0</BranchOrTag>
            </Source>
        </Component>
    </components>
</Config>
```

See the [`samples/`](samples/) directory for a ready-to-use example config file.

`<Component>/<Path>` is treated as relative to `<path>` by default. If `<Component>/<Path>` is absolute, it is used as-is. `%PATH%` is supported as a macro and is expanded to the top-level `<path>` value when the config is loaded.

### Key XML elements

| Element | Description |
|---|---|
| `<path>` | Base download directory on your machine (use an absolute path for predictable results) |
| `<Component>/<Path>` | Component target path: relative paths are resolved under `<path>`, absolute paths are used as-is; supports `%PATH%` to expand to top-level `<path>` |
| `<IsEnabled/>` | Self-closing tag -- marks a Source or Artifact as enabled by default |
| `<BranchOrTag>` | Git branch or tag to clone |
| `<NoShallow/>` | Opt out of shallow clone (full history) |
| `<version>` | Artifact version string |
| `<buildtype>` | Artifact build type (e.g. `Debug`, `Release`) |
| `<regex-include>` / `<regex-exclude>` | Filter which artifact files are downloaded |
| `<Script>` / `<script>` | Script run after the component is downloaded (in the component directory) |

---

## Troubleshooting

- **Components not appearing** -- check that the XML is valid and that `<IsEnabled/>` is present inside the `<Source>` or `<Artifact>` block you want enabled.
- **Authentication errors** -- confy reads Bitbucket credentials from `~/.m2/settings.xml`. Make sure your server ID and credentials are configured there.
- **View -> Debug Console** -- open the Debug Console for detailed logs of every network and git operation.

---

## For developers

### Build

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build -j
```

### Test

```bash
ctest --test-dir build --output-on-failure
```

### Architecture notes

- C++17, CMake, wxWidgets UI
- All network activity runs on background threads to keep the UI responsive
- Source downloads use the system `git` binary; artifact downloads use libcurl + Nexus REST API
