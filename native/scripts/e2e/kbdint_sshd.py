#!/usr/bin/env python3
"""SSH/SFTP test server with keyboard-interactive authentication (needs `pip install asyncssh`).

  kbdint_sshd.py <port> <root-dir> <mode>

Modes (user "twofa" in all of them):
  twostep   one challenge with TWO fields: "Password: " (hidden) + "Verification code: " (shown)
  rounds    two challenges in a row: first "Password: ", then "Verification code: "
  otponly   a single hidden field "One-time code: " (not the account password)
Accepted answers: password "pw-secret", code "246810". Password and public-key auth are disabled,
so a login only succeeds through the prompts. SFTP is served from <root-dir>.
"""
import asyncio, os, sys
import asyncssh

PASSWORD, CODE = "pw-secret", "246810"


class Server(asyncssh.SSHServer):
    def __init__(self, mode):
        self.mode = mode
        self.round = 0

    def begin_auth(self, username):
        return True

    def password_auth_supported(self):
        return False

    def public_key_auth_supported(self):
        return False

    def kbdint_auth_supported(self):
        return True

    def get_kbdint_challenge(self, username, lang, submethods):
        if self.mode == "twostep":
            return ("Two-factor login", "Enter your password and the code from your authenticator.", "en",
                    [("Password: ", False), ("Verification code: ", True)])
        if self.mode == "otponly":
            return ("", "", "en", [("One-time code: ", False)])
        return ("", "", "en", [("Password: ", False)])

    def validate_kbdint_response(self, username, responses):
        if username != "twofa":
            return False
        if self.mode == "twostep":
            return list(responses) == [PASSWORD, CODE]
        if self.mode == "otponly":
            return list(responses) == [CODE]
        if self.round == 0:
            if list(responses) != [PASSWORD]:
                return False
            self.round = 1
            return ("", "", "en", [("Verification code: ", True)])
        return list(responses) == [CODE]


async def main():
    port, root, mode = int(sys.argv[1]), sys.argv[2], sys.argv[3]
    keyfile = os.path.join(root, "..", "kbdint_host_key")
    if not os.path.exists(keyfile):
        asyncssh.generate_private_key("ssh-ed25519").write_private_key(keyfile)

    def sftp_factory(chan):
        return asyncssh.SFTPServer(chan, chroot=root)

    await asyncssh.create_server(lambda: Server(mode), "127.0.0.1", port, server_host_keys=[keyfile],
                                 sftp_factory=sftp_factory, allow_scp=False)
    print("kbdint sshd (%s) listening on 127.0.0.1:%d" % (mode, port), flush=True)
    await asyncio.Event().wait()


asyncio.run(main())
