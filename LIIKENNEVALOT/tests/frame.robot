*** Settings ***
Library    SerialLibrary
Suite Setup    Connect
Suite Teardown    Disconnect
Test Template    A-Time Should Parse To

*** Variables ***
${PORT}       COM3
${BAUD}       115200
${TIMEOUT}    1.0

*** Keywords ***
Connect
    Delete All Ports
    Add Port    ${PORT}    baudrate=${BAUD}    timeout=${TIMEOUT}    write_timeout=${TIMEOUT}    encoding=ascii
    Port Should Be Open    ${PORT}

Disconnect
    Delete All Ports

A-Time Should Parse To
    [Arguments]    ${HHMMSS}    ${EXPECTED}
    Write Data     A${HHMMSS}    encoding=ascii
    ${read}=       Read Until    terminator=58    encoding=ascii
    Should Be Equal As Strings    ${read}    ${EXPECTED}X

*** Test Cases ***
# --- Sekuntien rajat ---
Valid seconds 58 -> 58
    000058    58
Invalid seconds 60 -> -3
    000060    -3

# --- Minuuttien rajat ---
Valid minutes 59:00 -> 3540
    005900    3540
Invalid minutes 60 -> -3
    006000    -3

# --- Tuntien rajat ---
Valid 23:59:59 -> 86399
    235959    86399
Invalid 24:00:00 -> -3
    240000    -3



