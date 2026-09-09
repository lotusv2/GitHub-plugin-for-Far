# Сборка

## Требования

- Windows 10/11
- Visual Studio 2022 с workload **Desktop development with C++**
- CMake 3.20+
- Git

## Сборка

Из корня репозитория:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

Для 32-битного Far Manager:

```bat
cmake -S . -B build32 -A Win32
cmake --build build32 --config Release
```

CMake автоматически получает заголовки Far Manager из официального репозитория FarManager.

## Установка

Скопируйте `FarGitHub.dll` в отдельный каталог плагина Far Manager:

```text
%FARHOME%\Plugins\FarGitHub\FarGitHub.dll
```

## Настройка

На первом этапе используются переменные окружения:

```bat
setx FAR_GITHUB_TOKEN "github_pat_..."
setx FAR_GITHUB_REPOSITORY "lotusv2/GitHub-plugin-for-Far"
```

После изменения переменных перезапустите Far Manager.

## Использование

Откройте `F11` → `GitHub`.

- Enter на каталоге открывает каталог.
- `Backspace` / переход в `..` возвращает выше.
- Enter на файле загружает его во временный файл и открывает редактор Far.
- После сохранения изменённое содержимое отправляется коммитом в GitHub.
- `F7` создаёт каталог через `.gitkeep`.

Это ранняя версия. Управление репозиториями, ветками, удаление, копирование файлов и полноценные настройки будут добавлены следующим этапом.
