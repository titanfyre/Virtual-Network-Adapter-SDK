# Virtual-Network-Adapter-SDK — SDK et installation

Projet : [Virtual-Network-Adapter-SDK](https://github.com/titanfyre/Virtual-Network-Adapter-SDK). [Sources et documentation des SDK](https://github.com/TitanCSA/sdk-packages/tree/main/projects/Virtual-Network-Adapter-SDK).

Choisis le paquet a installer. Installe son outil habituel (Node.js/npm, Python/pip, .NET, etc.), puis execute la commande dans le dossier de ton application. Les pilotes et applications requis restent a installer separement selon le README du projet.

Les SDK natifs sont destines a Windows x64. Java utilise Java22+ (Kotlin passe par le SDK Java). Flutter utilise le SDK Dart ; le chargement de la DLL doit etre configure dans l application. AutoHotInterceptor .NET utilise .NET Framework4.8.

| Registre | Paquet | Installation / utilisation |
| --- | --- | --- |
| vcpkg | `virtual-network-adapter-sdk` | `vcpkg install virtual-network-adapter-sdk:x64-windows` |
| Conan | `virtual-network-adapter-sdk` | `conan install --requires=virtual-network-adapter-sdk/0.1.1 -r titancsa --build=missing -s compiler.cppstd=17` |
| NuGet | `Virtual-Network-Adapter-SDK.Native` | `Install-Package Virtual-Network-Adapter-SDK.Native -Version 0.1.1` |
| NuGet | `VirtualNetworkAdapterSDK.SDK` | `dotnet add package VirtualNetworkAdapterSDK.SDK --version 1.0.0` |
| PyPI | `virtual_network_adapter_sdk` | Commande prevue apres publication : `python -m pip install virtual-network-adapter-sdk==1.0.0` |
| Conda | `virtual-network-adapter-sdk` | `conda install --override-channels -c https://raw.githubusercontent.com/TitanCSA/sdk-packages/089bc73095aa8eda89bacbe10d0b9e58394b0207/conda-channel -c conda-forge virtual-network-adapter-sdk` (canal TitanCSA) |
| crates.io | `virtual_network_adapter_sdk` | `cargo add virtual-network-adapter-sdk@1.0.0` |
| npm | `virtual_network_adapter_sdk` | Commande prevue apres publication : `npm install virtual-network-adapter-sdk@1.0.0` |
| Maven Central | `io.github.titancsa:virtual-network-adapter-sdk` | `io.github.titancsa:virtual-network-adapter-sdk:1.0.0 (pom.xml / Gradle)` |
| JitPack | `com.github.titanfyre:Virtual-Network-Adapter-SDK` | `com.github.titanfyre:Virtual-Network-Adapter-SDK:java-sdk-v1.0.0 (avec le depot JitPack)` |
| Go Modules | `github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules` | `go get github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules@v1.0.0` |
| pkg.go.dev | `github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules` | `Documentation : https://pkg.go.dev/github.com/titanfyre/Virtual-Network-Adapter-SDK/Virtual-Network-Adapter-SDK-Go-modules@v1.0.0` |
| PowerShell Gallery | `virtual_network_adapter_sdk` | `Install-Module VirtualNetworkAdapterSDK -RequiredVersion 1.0.0 -Scope CurrentUser` |
| pub.dev | `virtual_network_adapter_sdk` | Commande prevue apres publication : `dart pub add virtual_network_adapter_sdk:1.0.0` |
| RubyGems | `virtual_network_adapter_sdk` | `gem install virtual-network-adapter-sdk -v 1.0.0` |
| Packagist | `virtual_network_adapter_sdk` | Commande prevue apres publication : `composer require titancsa/virtual-network-adapter-sdk:1.0.0` |

## Configuration vcpkg et Conan

Les sources et headers C / C++ sont dans le dossier central de ce projet. Les headers C se trouvent dans les dossiers `*-C-ffi` ou `*-C-json` (Interceptor-MultiLang : `native/`). Les paquets natifs C++ et la disponibilite du bridge C sont distincts ; consulte leur README avant de lier une application C.

Pour vcpkg, ajoute un fichier `vcpkg-configuration.json` a cote de ton manifeste `vcpkg.json` :

```json
{
  "registries": [
    {
      "kind": "git",
      "repository": "https://github.com/TitanCSA/vcpkg-registry",
      "baseline": "9a4b8cb920d4cfc97ce6d7af461bcc7508392a68",
      "packages": [
        "virtual-network-adapter-sdk",
        "virtual-network-adapter-sdk-sdk"
      ]
    }
  ]
}
```

Pour Conan2, configure une seule fois l index source :

```powershell
git clone https://github.com/TitanCSA/sdk-packages.git
conan remote add titancsa ./sdk-packages/conan-index -t local-recipes-index
```

Execute ensuite la commande Conan du tableau. Ce registre construit les bibliotheques depuis leurs sources ; il est distinct de ConanCenter. La commande NuGet native s execute dans la console du gestionnaire de packages de Visual Studio.

## Configuration Maven et JitPack

Dans le bloc `<dependencies>` de ton `pom.xml` :

```xml
<dependency>
  <groupId>io.github.titancsa</groupId>
  <artifactId>virtual-network-adapter-sdk</artifactId>
  <version>1.0.0</version>
</dependency>
```

Avec Gradle Kotlin :

```kotlin
implementation("io.github.titancsa:virtual-network-adapter-sdk:1.0.0")
```

Pour JitPack, ajoute aussi le depot `https://jitpack.io`, puis utilise les coordonnees du tableau. Les versions Go et JitPack deja publiees continuent de pointer vers leurs anciens tags immuables dans le depot du projet : ces tags restent disponibles. Leurs sources de travail sont maintenant centralisees ici ; aucun ancien import Go n est renomme.

## Etat et documentation

Les commandes marquees « prevues » ne sont pas encore des installations disponibles. Le canal Conda TitanCSA n est pas le catalogue officiel conda-forge. Les versions et licences existantes ne sont pas remplacees par cette reorganisation.

Les guides API et exemples de chaque langage se trouvent dans leurs dossiers SDK, a cote des sources. Les bindings peuvent exposer une partie de l API C++ ; les limites restent documentees dans chaque SDK.

[Retour au catalogue central](../../README.md)
