// SPDX-License-Identifier: GPL-2.0-only
/*
 *  dict.c - Guarde informações globalmente
 */
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>   /* Needed for __init/__exit */
#include <linux/module.h> /* Needed for module_init/module_exit/MODULE_* */
#include <linux/string.h>
#include <linux/uaccess.h>

/// Parte do Dicionário

#define MAX_SIZE 100 // Número máximo de linhas no hashmap

struct node {
  char *key; // String terminada em \0
  char *val; // String terminada em \0
  struct node *next;
};

char *last_val = NULL;

// TODO: Ver se tem problema ser instanciado comptime
struct node hashmap[MAX_SIZE] = {(struct node){
    .key = NULL,
    .val = NULL,
    .next = NULL,
}};

static int make_hash(char key[]) {
  pr_info("fazendo o hash\n");
  int hash = 0;
  for (int i = 0; key[i] != '\0'; i++) {
    hash = (hash * 15 + key[i]) % MAX_SIZE;
  }
  return hash;
}

static void insert(char key[], char val[]) {

  if (key == NULL) {
    return;
  }

  int hash = make_hash(key);
  pr_info("inserindo %s=%s", key, val);

  if (hashmap[hash].key == NULL) {
    hashmap[hash] = (struct node){
        .key = kmalloc(sizeof(char) * strlen(key) + 1, GFP_KERNEL),
        .val = kmalloc(sizeof(char) * strlen(val) + 1, GFP_KERNEL),
        .next = NULL,
    };

    strcpy(hashmap[hash].key, key);
    strcpy(hashmap[hash].val, val);
    return;
  }

  struct node node = hashmap[hash];

  while (node.next->key != NULL) {
    node = *node.next;
  }

  node.next = kmalloc(sizeof(struct node), GFP_KERNEL);
  node.next->key = kmalloc(sizeof(char) * strlen(key) + 1, GFP_KERNEL);
  strcpy(node.next->key, key);
  node.next->key = kmalloc(sizeof(char) * strlen(val) + 1, GFP_KERNEL);
  strcpy(node.next->val, val);
  node.next->next = NULL;
}

static void clear_hashmap(void) {
  for (int i = 0; i < MAX_SIZE; i++) {

    if (hashmap[i].key == NULL) {
      continue;
    }

    // Não precisamos limpar o primeiro (talvez)
    struct node *node_atual;
    struct node *next = hashmap[i].next;

    while (next != NULL) {

      node_atual = next;
      next = next->next;
      kfree(node_atual->key);
      kfree(node_atual->val);
      kfree(node_atual);
    }
  }
}

static char *get(char key[]) {

  if (key == NULL) {
    return NULL;
  }

  int hash = make_hash(key);

  if (hashmap[hash].key == NULL) {
    pr_err("Não há valor associado a essa chave\n");
    return NULL;
  }

  struct node node = hashmap[hash];

  while (node.key != NULL && strcmp(node.key, key) != 0) {
    node = *node.next;
  }

  if (strcmp(node.key, key) != 0) {
    pr_err("Não há valor associado a essa chave\n");
    return NULL;
  }
  last_val = node.val;
  return node.val;
}

/// Parte do driver

static dev_t dict_dev;        // Holds the major and minor number for our driver
static struct cdev dict_cdev; // Char device. Holds fops and device number

static ssize_t dict_read(struct file *file, char __user *buf, size_t size,
                         loff_t *ppos) {
  // TODO: implementar ele printar o último valor adicionado

  // Return the string corresponding to the current driver state
  return simple_read_from_buffer(buf, size, ppos, last_val, strlen(last_val));
  return 0;
}

static ssize_t dict_write(struct file *file, const char __user *buf,
                          size_t size, loff_t *ppos) {
  char value;

  char *cmd = kmalloc(sizeof(char) * (size + 1), GFP_KERNEL);
  size_t separator = 0;

  size_t i = 0;
  do {
    if (copy_from_user(&value, buf + i, 1))
      return -EFAULT;

    if (value == ' ') {
      pr_err("Não aceitamos espaços >:(\n");
      return -EFAULT;
    }
    if (value == '\n') {
      break;
    }

    cmd[i] = value;

    if (value == '=') {
      separator = i;
    }

    i++;
  } while (value != '\n' && value != '\0' && i < size);

  if (separator != 0) {
    char *key = kmalloc(sizeof(char) * (separator + 1), GFP_KERNEL);
    strncpy(key, cmd, separator);
    key[separator] = '\0';

    char *val = NULL;
    size_t val_size = i - separator;
    val = kmalloc(sizeof(char) * val_size, GFP_KERNEL); // já considera '\0'
    strncpy(val, cmd + separator + 1, val_size - 1);
    val[val_size - 1] = '\0';
    pr_info("Chave: '%s'\n", key);
    pr_info("Valor: '%s'\n", val);
    insert(key, val);
    pr_info("valor adicionado no dicionário: %s\n", get(key));

    kfree(key);
    kfree(val);

  } else {

    char *key = kmalloc(sizeof(char) * (i + 1), GFP_KERNEL);
    strncpy(key, cmd, i + 1);
    key[i] = '\0';
    if(get(key) == NULL)
      return -EFAULT;
    kfree(key);
  }

  kfree(cmd);

  return size; // quantos caracteres queremos receber (?)
}

// Define the functions that implement our file operations
static struct file_operations dict_fops = {
    .read = dict_read,
    .write = dict_write,
};

static int __init dict_init(void) {
  int ret;

  // Allocate a major and a minor number
  ret = alloc_chrdev_region(&dict_dev, 0, 1, "dict");
  if (ret)
    pr_err("Failed to allocate device number\n");

  // Initialize our character device structure
  cdev_init(&dict_cdev, &dict_fops);

  // Register our character device to our device number
  ret = cdev_add(&dict_cdev, dict_dev, 1);
  if (ret)
    pr_err("Char device registration failed\n");

  pr_info("Dicionário inicializado\n");

  return 0;
}

static void __exit dict_exit(void) {
  // Clean up our mess
  cdev_del(&dict_cdev);
  unregister_chrdev_region(dict_dev, 1);

  pr_info("Apagando o dicionário\n");
  clear_hashmap();
}

module_init(dict_init);
module_exit(dict_exit);

MODULE_AUTHOR("Giancarlo Bonvenuto, Matheus Veiga, Julio Nunes, José Victor");
MODULE_DESCRIPTION(
    "Uma forma de guardar variáveis globalmente utilizando device drivers");
MODULE_LICENSE("GPL");
