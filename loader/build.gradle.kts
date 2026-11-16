import java.nio.file.Paths
import java.util.Locale

plugins {
    alias(libs.plugins.agp.lib)
}

val verCode: Int by rootProject.extra
val verName: String by rootProject.extra
val commitHash: String by rootProject.extra
val workDirectory: String by rootProject.extra

fun Project.findInPath(executable: String, property: String): String? {
    val pathEnv = System.getenv("PATH")
    val isWindows = System.getProperty("os.name").lowercase(Locale.ROOT).contains("win")
    return pathEnv.split(File.pathSeparator).map { folder ->
        Paths.get("${folder}${File.separator}${executable}${if (isWindows) ".exe" else ""}")
            .toFile()
    }.firstOrNull { path ->
        path.exists()
    }?.absolutePath ?: properties.getOrDefault(property, null) as? String?
}

val ccachePath by lazy {
    project.findInPath("ccache", "ccache.path")?.also {
        println("loader: Use ccache: $it")
    }
}

val defaultCFlags = arrayOf(
    "-Wall", "-Wextra",
    "-fno-rtti", "-fno-exceptions",
    "-fno-stack-protector", "-fomit-frame-pointer",
    "-Wno-builtin-macro-redefined", "-D__FILE__=__FILE_NAME__"
)

android {
    buildFeatures {
        androidResources = false
        buildConfig = false
        prefab = true
    }

    externalNativeBuild.cmake {
        path("src/CMakeLists.txt")
        buildStagingDirectory = layout.buildDirectory.get().asFile
    }

    defaultConfig {
        // Ship native loader binaries for the single supported Android ABI.
        ndk {
            abiFilters += setOf("arm64-v8a")
        }

        externalNativeBuild.cmake {
            cFlags("-std=c18", *defaultCFlags)
            cppFlags("-std=c++20", *defaultCFlags)
            // WORK_DIRECTORY travels as a CMake argument (like ZKSU_VERSION
            // below) and is turned into a string-literal macro by
            // `add_definitions` in src/CMakeLists.txt. Quoting it inside a
            // cFlag is fragile: AGP's flag pipeline on Windows strips the
            // quotes and clang ends up with a multi-char literal.
            arguments += "-DWORK_DIRECTORY=${workDirectory}"
            ccachePath?.let {
                arguments += "-DNDK_CCACHE=$it"
            }
        }
    }

    buildTypes {
        debug {
            externalNativeBuild.cmake {
                arguments += "-DZKSU_VERSION=$verName-$verCode-$commitHash-debug"
            }
        }
        release {
            externalNativeBuild.cmake {
                arguments += "-DZKSU_VERSION=$verName-$verCode-$commitHash-release"
            }
        }
    }
}
