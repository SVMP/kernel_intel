/*
 * efibc: control EFI bootloaders which obey LoaderEntryOneShot var
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*
 * This driver intercepts system reboot requests and populates the
 * LoaderEntryOneShot EFI variable with the user-supplied reboot argument. EFI
 * bootloaders such as Gummiboot will consume this variable and use it to
 * control which OS is booted next.
 */

#include <linux/efi.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/reboot.h>

#define LOADER_ENTRY_ONE_SHOT "LoaderEntryOneShot"
#define LOADER_GUID EFI_GUID(0x4a67b082, 0x0a4c, 0x41cf, 0xb6, 0xc7, \
				0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f)

/*
 * Convert char string to efi_char16_t string. Null byte at end is always
 * preserved.
 */
unsigned long
efichar_from_char(efi_char16_t *dest, const char *src, size_t dest_len)
{
	int i;
	for (i = 0; src[i] && i < (dest_len/sizeof(*dest)) - 1; i++)
		dest[i] = src[i];
	dest[i] = 0; // Gummiboot does need nul-terminated strings
	return i;
}

/*
 * Returns the number of characters in an efi_char16_t string. Does not include
 * terminating nul.
 */
static unsigned long efi_char16_strlen(efi_char16_t *s)
{
	unsigned long length = 0;

	while (*s++ != 0)
		length++;
	return length;
}

/*
 *  Returns required size of utf16 buffer for given char string. Space for
 *  trailing nul is included. str must not be NULL.
 */
static size_t efi_char16_bufsz(char *str)
{
	return (1+strlen(str))*sizeof(efi_char16_t);
}

/*
 * Set EFI variable to specify next boot target for EFI bootloader. Meant to be
 * called as a reboot notifier. DO NOT call this function without guaranteeing
 * efi_enabled == true; it will crash.
 */
static int efibc_reboot_notifier_call(
		struct notifier_block *notifier,
		unsigned long what, void *data)
{
	int ret = NOTIFY_DONE;
	char *cmd = (char *) data;
	efi_status_t status = EFI_NOT_FOUND;
	efi_char16_t *name_efichar = NULL;
	efi_char16_t *cmd_efichar = NULL;
	size_t name_efichar_blen = efi_char16_bufsz(LOADER_ENTRY_ONE_SHOT);
	size_t cmd_efichar_blen = 0;

	if (what != SYS_RESTART || cmd == NULL)
		goto out1;

	cmd_efichar_blen = efi_char16_bufsz(cmd);

	name_efichar = kzalloc(name_efichar_blen, GFP_KERNEL);
	if (name_efichar == NULL) {
		pr_err("efibc: %s: failed to allocate memory.\n", __func__);
		goto out1;
	}

	cmd_efichar = kzalloc(cmd_efichar_blen, GFP_KERNEL);
	if (cmd_efichar == NULL) {
		pr_err("efibc: %s: failed to allocate memory.\n", __func__);
		goto out2;
	}

	if (efichar_from_char(name_efichar, LOADER_ENTRY_ONE_SHOT,
			name_efichar_blen) != strlen(LOADER_ENTRY_ONE_SHOT)) {
		pr_err("efibc: %s: Failed to convert char to efi_char16_t. length=%lu",
			__func__, name_efichar_blen);
		goto out3;
	}

	if (efichar_from_char(cmd_efichar, cmd, cmd_efichar_blen)
			!= strlen(cmd)) {
		pr_err("efibc: %s: Failed to convert char to efi_char16_t. length=%lu",
			__func__, cmd_efichar_blen);
		goto out3;
	}

	status = efi.set_variable(
			name_efichar,
			&LOADER_GUID,
			EFI_VARIABLE_NON_VOLATILE
				| EFI_VARIABLE_BOOTSERVICE_ACCESS
				| EFI_VARIABLE_RUNTIME_ACCESS,
			cmd_efichar_blen,
			cmd_efichar);

	if (status != EFI_SUCCESS) {
		pr_err("efibc: set_variable() failed. " "status=%lx\n", status);
		goto out3;
	}

	ret = NOTIFY_OK;

out3:
	kfree(cmd_efichar);
out2:
	kfree(name_efichar);
out1:
	return ret;
}

static struct notifier_block efibc_reboot_notifier = {
	.notifier_call = efibc_reboot_notifier_call,
};

static int __init efibc_init(void)
{
	if (!efi_enabled(EFI_RUNTIME_SERVICES))
		return 0;

	if (register_reboot_notifier(&efibc_reboot_notifier)) {
		pr_err("efibc: unable to register reboot notifier\n");
		return -1;
	}

	return 0;
}
module_init(efibc_init);

static void __exit efibc_exit(void)
{
	unregister_reboot_notifier(&efibc_reboot_notifier);
}
module_exit(efibc_exit);

MODULE_AUTHOR("Matt Gumbel <matthew.k.gumbel@intel.com");
MODULE_DESCRIPTION("EFI bootloader communication module");
MODULE_LICENSE("GPL v2");
