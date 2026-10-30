#!/bin/bash
# ============================================================
# test.sh — End-to-end test for the Multi-Client Email Service
# ============================================================
#
# Prerequisites:
#   1. Server must be running:  ./server
#   2. Run this script:         bash test.sh
#
# This script uses the compiled client binary with scripted input
# via heredocs to test all functionality.
# ============================================================

set -e

SERVER_IP="127.0.0.1"
PORT="8888"
CLIENT="./client"
PASS=0
FAIL=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

check_result() {
    local test_name="$1"
    local output="$2"
    local expected="$3"

    if echo "$output" | grep -q "$expected"; then
        echo -e "  ${GREEN}✓ PASS${NC}: $test_name"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗ FAIL${NC}: $test_name (expected '$expected')"
        echo "    Output: $(echo "$output" | head -5)"
        FAIL=$((FAIL + 1))
    fi
}

echo ""
echo "============================================="
echo "  Multi-Client Email Service — Test Suite"
echo "============================================="
echo ""

# Clean up any previous test data
# NOTE: The server must be restarted after cleaning data so it re-runs storage_init().
# If the server is already running, it will recreate dirs on first register.
echo "Ready to test. Make sure the server is running: ./server"
echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 1: Register three users (alice, bob, amit)${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "1\nalice\npassword123\nalice@iitlab.local\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Register alice" "$OUTPUT" "REGISTER_OK"

OUTPUT=$(echo -e "1\nbob\npass456\nbob@iitlab.local\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Register bob" "$OUTPUT" "REGISTER_OK"

OUTPUT=$(echo -e "1\namit\namit789\namit@iitlab.local\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Register amit" "$OUTPUT" "REGISTER_OK"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 2: Duplicate registration${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "1\nalice\notherpass\nalice@iitlab.local\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Reject duplicate alice" "$OUTPUT" "REGISTER_FAIL"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 3: Login with valid credentials${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\npassword123\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Login alice succeeds" "$OUTPUT" "Login successful"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 4: Login with wrong password${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\nwrongpass\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Wrong password rejected" "$OUTPUT" "Login failed"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 5: Login with non-existent user${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nnouser\npass\n3\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Non-existent user rejected" "$OUTPUT" "Login failed"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 6: Send emails (alice → bob, alice → amit)${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\npassword123\n1\nbob\nInternet Technology Lab\nPlease complete the socket programming experiment.\n1\namit\nMeeting\nLab meeting at 3 PM tomorrow.\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Send email to bob" "$OUTPUT" "SEND_OK"
check_result "Send email to amit" "$OUTPUT" "SEND_OK"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 7: Send email to non-existent user${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\npassword123\n1\nghost\nTest\nThis should fail\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Send to non-existent user fails" "$OUTPUT" "SEND_FAIL"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 8: Check bob's inbox${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nbob\npass456\n2\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Bob's inbox shows email" "$OUTPUT" "alice"
check_result "Bob's inbox shows subject" "$OUTPUT" "Internet Technology Lab"
check_result "Inbox shows UNREAD status" "$OUTPUT" "UNREAD"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 9: Read email (bob reads email #1)${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nbob\npass456\n3\n1\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Read email displays sender" "$OUTPUT" "From:    alice"
check_result "Read email displays subject" "$OUTPUT" "Subject: Internet Technology Lab"
check_result "Read email displays body" "$OUTPUT" "socket programming"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 10: Check alice's sent mail${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\npassword123\n4\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Sent mail shows bob" "$OUTPUT" "bob"
check_result "Sent mail shows amit" "$OUTPUT" "amit"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 11: Logout and re-login${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nalice\npassword123\n5\n2\nalice\npassword123\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Logout succeeds" "$OUTPUT" "Logged out successfully"
check_result "Re-login succeeds" "$OUTPUT" "Login successful"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 12: Check amit's inbox${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\namit\namit789\n2\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Amit's inbox shows email from alice" "$OUTPUT" "alice"
check_result "Amit's inbox shows Meeting subject" "$OUTPUT" "Meeting"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 13: Empty inbox (bob sends nothing, check his sent)${NC}"
# ------------------------------------------------------------------

OUTPUT=$(echo -e "2\nbob\npass456\n4\n6\n" | $CLIENT $SERVER_IP $PORT 2>&1 || true)
check_result "Bob has no sent emails" "$OUTPUT" "No sent emails"

echo ""

# ------------------------------------------------------------------
echo -e "${YELLOW}TEST 14: Data persistence check${NC}"
# ------------------------------------------------------------------

if [ -f "data/users.dat" ]; then
    USERS=$(cat data/users.dat)
    check_result "users.dat contains alice" "$USERS" "alice"
    check_result "users.dat contains bob" "$USERS" "bob"
    check_result "users.dat contains amit" "$USERS" "amit"
else
    echo -e "  ${RED}✗ FAIL${NC}: data/users.dat not found"
    FAIL=$((FAIL + 1))
fi

if [ -d "data/mail/alice/inbox" ] && [ -d "data/mail/alice/sent" ]; then
    echo -e "  ${GREEN}✓ PASS${NC}: alice mail directories exist"
    PASS=$((PASS + 1))
else
    echo -e "  ${RED}✗ FAIL${NC}: alice mail directories missing"
    FAIL=$((FAIL + 1))
fi

if [ -d "data/mail/bob/inbox" ]; then
    MAIL_FILES=$(ls data/mail/bob/inbox/*.mail 2>/dev/null | wc -l | tr -d ' ')
    if [ "$MAIL_FILES" -ge 1 ]; then
        echo -e "  ${GREEN}✓ PASS${NC}: bob has $MAIL_FILES email(s) in inbox"
        PASS=$((PASS + 1))
    else
        echo -e "  ${RED}✗ FAIL${NC}: bob inbox is empty on disk"
        FAIL=$((FAIL + 1))
    fi
fi

echo ""

# ------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------
TOTAL=$((PASS + FAIL))
echo "============================================="
echo -e "  Results: ${GREEN}$PASS passed${NC}, ${RED}$FAIL failed${NC} out of $TOTAL tests"
echo "============================================="
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
else
    echo -e "${RED}Some tests failed. Check output above.${NC}"
fi

exit $FAIL
