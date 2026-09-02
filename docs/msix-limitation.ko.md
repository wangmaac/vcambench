# 가상 카메라는 MSIX로 배포할 수 없다

[English](msix-limitation.md) | 한국어

조사일: 2026-08-24 · 환경: Windows 11 (26100 / 26200), Windows SDK 10.0.26100

## 결론

**Windows 가상 카메라는 MSIX 패키지만으로는 동작하지 않는다.**
빌드도 패키징도 설치도 되지만, 카메라가 켜지지 않는다.
`regsvr32`로 `HKLM`에 등록하는 고전 설치 프로그램(MSI/EXE)이 필요하다.

Microsoft 스토어 배포 자체가 막히는 것은 아니다. 스토어는 2021년부터 MSIX가 아닌
`.msi`/`.exe` 설치 프로그램도 등록받는다. 다만 그 경로는 **코드 서명 인증서가 필수**다.

## 왜 그런가

가상 카메라의 프레임 생성 코드(DLL)는 **우리 앱이 아니라 Windows Camera Frame Server가
자기 프로세스에 로드해서 실행**한다. 이 서비스는 `svchost.exe` 안에서 **LOCAL SERVICE**
계정으로 돈다.

```
내 앱 (vcamhost.exe)
   │  MFCreateVirtualCamera("이 CLSID로 카메라 만들어줘")
   │  여기서 앱의 역할 끝 — 영상 경로에 없다
   ▼
Windows Camera Frame Server   ← svchost.exe / LOCAL SERVICE
   │  1. 레지스트리에서 CLSID 조회
   │  2. CoCreateInstance → 우리 DLL 을 자기 프로세스에 로드
   │  3. 33ms 마다 프레임 요청
   ▼
카메라 앱 / Chrome / Zoom / …
```

DLL을 로드하는 주체가 **다른 계정의 시스템 서비스**라는 것이 핵심이다.

| 등록 방식 | 등록 위치 | LOCAL SERVICE 가 볼 수 있나 |
|---|---|---|
| `regsvr32` (MSI/EXE) | `HKLM` | **볼 수 있다** |
| MSIX `com4:InProcessServer` | 사용자 단위 패키지 카탈로그 | **볼 수 없다** |

MSIX 패키지는 본질적으로 사용자 단위로 등록된다. 서비스 계정에는 그 패키지가 등록된 적이
없으므로 패키지 상대 경로를 해석하지 못한다.

이는 Microsoft 문서의 서술과도 일치한다 — 미디어 소스는 `HKLM`에 등록해야 하며
"여러 프로세스가 로드하므로 `HKCU` 등록은 불가능"하다. MSIX의 사용자 단위 등록은
`HKCU`와 같은 처지다.

## 실험과 증거

전용 CLSID를 가진 최소 MSIX 패키지를 만들어 `com4:InProcessServer`로 미디어 소스를
등록했다(개발자 모드 loose 등록, 서명 불필요). 기존에 동작 중인 설치본과 간섭하지 않도록
CLSID를 분리했다.

| # | 확인 | 결과 |
|---|---|---|
| 1 | C++ 빌드, MSIX 매니페스트 검증, 패키지 등록 | **모두 성공** |
| 2 | `MFCreateVirtualCamera` | **성공** |
| 3 | `IMFVirtualCamera::Start()` | **실패 — `0x80070003` (경로를 찾을 수 없음)** |
| 4 | 카메라 목록 표시 | 표시되지 않음 |
| 5 | 패키지를 `C:\Users\Public` 로 옮겨 재등록 | **동일 실패** → 폴더 권한 문제 아님 |
| 6 | 패키지 **밖의 일반 프로세스**가 같은 CLSID 를 `CoCreateInstance(INPROC)` | **성공 (S_OK)** |

3번이 `REGDB_E_CLASSNOTREG`가 **아니라는 점**이 결정적이다. 등록은 찾았는데 DLL 경로를
열지 못했다는 뜻이다.

6번이 원인을 확정한다. **같은 사용자 세션의 일반 프로세스는 그 CLSID 를 정상적으로
로드한다.** 등록이 안 된 것이 아니라, 볼 수 있는 주체가 등록한 사용자 하나뿐이다.
실패하는 쪽은 계정이 다른 Frame Server 뿐이다.

## 검증하지 않은 것

정식 서명해서 `C:\Program Files\WindowsApps` 에 설치한 MSIX 는 시험하지 않았다.
결론이 바뀌지 않으리라 보는 근거는 두 가지다.

- 막고 있는 것은 파일 위치가 아니라 **사용자 단위 등록**이다
- 파일 접근 가능성 가설은 실험 5번에서 이미 배제됐다

확실히 못 박으려면 자체 서명 인증서로 정식 MSIX 를 만들어 재시험하면 된다.

## 그래서 어떻게 배포하는가

| | MSIX | MSI / EXE |
|---|---|---|
| 가상 카메라 동작 | **불가** | 가능 (Microsoft 공식 샘플과 동일한 방식) |
| 코드 서명 | Microsoft 가 대신 서명 (무료) | **직접 구매해 서명** |
| 스토어 등록 | 가능 | 가능 (설치 파일 링크 제출) |
| 설치·업데이트 | OS 가 처리 | 직접 구현 |

가상 카메라 제품은 MSI/EXE 를 선택할 수밖에 없고, 그 순간 코드 서명이 우리 몫이 된다.
**서명이 무료인 유일한 경로(스토어 MSIX)가 정확히 우리가 쓸 수 없는 경로다.**

이 프로젝트가 실제로 어느 경로를 택했는지는 README 의 [코드 서명 정책](../README.ko.md#코드-서명-정책-code-signing-policy)에 있다.

서명된 파일이라도 **타임스탬프를 함께 찍으면 인증서 만료 후에도 서명은 계속 유효**하다.
다만 새 빌드마다 새 서명이 필요하므로, 업데이트를 내보내는 제품이라면 갱신은 사실상 필수다.

## 참고

- [Windows-Camera VirtualCamera 샘플](https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera) — Microsoft 공식 샘플, MSI 로 배포
- [com4:ComServer 매니페스트 스키마](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-com4-comserver)
- [Code signing options for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
- [Choose a distribution path for your Windows app](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path)
