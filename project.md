Project

This is an app that is used to download a set of artifacts (or source) from a remote server (e.g. bitbucket, or nexus) to the local machine.

The set of artifacts is defined by an XML file, which looks something like this:

<config>
    <version>9</version>
    <path>path/to/local/storage</path>
    <components>
        <Component>
            <name>my_name</name>
            <DisplayName>My Name</displayName>
            <Path>target download directory relative to top level "path"</Path>
            <Source>
                <IsEnabled/>
                <url>bitbucket.../project/myrepo.git</url>
                <BranchOrTag>master</BranchOrTag>
                <Script>somePostRunScript</Script>
            </Source>
            <Artifact>
                <IsEnabled/>
                <url>nexus-repository.myurl.com/#browse/browse:my-raw-repository</url>
                <version>myProduct<version>
                <buildtype>Debug</buildtype>
                <script>postBuildScript</script>
            </Artifact>
        <Component>
        ... many components
    </Components>
</Config>

Each component is either source-based (e.g. pull from bitbucket), artifact based (e.g. pull from nexus), or both, as determined by the presence of <IsEnabled/>.

Upon opening (or through File > Load) the app allows the user to select an XML configuration to load.

The app then loads the configuration specified, and creates a user-friendly window showing the list of the components, and a subsection for each {source, artifact} of the given component with a checkbox to enable/disable downloading that component.

The user has the option to use the dropdown to select different branch/tags (for source) or versions/build types (for artifacts).

The window has an "apply" button which will proceed to download all of the sources and artifacts selected to the desired target download directory. Sources are cloned via a shallow clone from git, and artifacts are downloaded from nexus-repository....my-raw-repository/name/version/buildtype/*

The application is responsive even while making network requests and shows active status to avoid the user feeling the app is hung.

Implementation notes:
- App is CMake, C++17 based, cross-platform (wxwidgets UI)
- Minimize third party dependencies unless there is significant value in using it
- All network activity is dispatched out to a separate thread to keep UI responsive

Basic TODO list: (update as we go)

- [x] Set up basic build infrastructure (initial CMake + wxWidgets app skeleton)
- [x] Sketch out what UI looks like (initial list + load/apply window, to be iterated)
- [~] Parsing XML into an object matching our semantics (initial loader implemented; needs hardening)
- [ ] Develop helper code/library to pull from Bitbucket and Nexus
    - requires token-based authentication
- [ ] Implement settings window, reading/saving to a user-specific config file