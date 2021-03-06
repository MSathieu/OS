#include <sorted_list.h>

void insert_sorted_list(struct sorted_list* list, struct linked_list_member* new_member) {
  new_member->next = 0;
  struct linked_list_member* prev_member = 0;
  for (struct linked_list_member* member = list->first; member; member = member->next) {
    if (list->compare(new_member, member) <= 0) {
      if (prev_member) {
        prev_member->next = new_member;
      } else {
        list->first = new_member;
      }
      new_member->next = member;
      return;
    }
    prev_member = member;
  }
  if (prev_member) {
    prev_member->next = new_member;
  } else {
    list->first = new_member;
  }
}
