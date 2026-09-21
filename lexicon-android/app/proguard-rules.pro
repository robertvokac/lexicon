# kotlinx.serialization and Navigation ship their own R8 rules; the REST
# models and navigation routes need nothing beyond them.

# Keep the names in stack traces readable in crash reports from release builds.
-keepattributes SourceFile,LineNumberTable
-renamesourcefileattribute SourceFile
