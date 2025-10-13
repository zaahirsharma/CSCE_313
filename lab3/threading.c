#include "threading.h"

// Track which stacks we've allocated
static void *allocated_stacks[NUM_CTX] = {NULL};

void t_init()
{
        // Initialize all contexts to INVALID state
        for (int i = 0; i < NUM_CTX; i++)
        {
                contexts[i].state = INVALID;
                memset(&contexts[i].context, 0, sizeof(ucontext_t));
                allocated_stacks[i] = NULL;
        }
        
        // Set the main thread context at index 0
        current_context_idx = 0;
        contexts[0].state = VALID;
        
        // Capture the main thread's context
        getcontext(&contexts[0].context);
}

int32_t t_create(fptr foo, int32_t arg1, int32_t arg2)
{
        // Find the next free slot in the contexts array
        volatile int32_t next = -1;
        for (volatile int i = 1; i < NUM_CTX; i++)  // Start from 1, since 0 is main
        {
                if (contexts[i].state == INVALID)
                {
                        next = i;
                        break;
                }
        }
        
        // If no free slot found, return error
        if (next == -1)
        {
                return 1;
        }
        
        // Initialize the context
        getcontext(&contexts[next].context);
        
        // Allocate stack for this context
        void *stack = malloc(STK_SZ);
        if (stack == NULL)
        {
                return 1;  // Memory allocation failed
        }
        
        // Store the stack pointer for later freeing
        allocated_stacks[next] = stack;
        
        contexts[next].context.uc_stack.ss_sp = stack;
        contexts[next].context.uc_stack.ss_size = STK_SZ;
        contexts[next].context.uc_stack.ss_flags = 0;
        contexts[next].context.uc_link = NULL;
        
        // Create the context to execute the function with given arguments
        makecontext(&contexts[next].context, (ctx_ptr)foo, 2, arg1, arg2);
        
        // Mark this context as VALID and ready to run
        contexts[next].state = VALID;
        
        return 0;  // Success
}

int32_t t_yield()
{
        // Save the current context index
        uint8_t current = current_context_idx;
        
        // Find the next valid context to switch to (round-robin)
        int32_t next = -1;
        for (int i = 1; i <= NUM_CTX; i++)
        {
                int idx = ((int)current + i) % NUM_CTX;
                if (contexts[idx].state == VALID && idx != (int)current)
                {
                        next = idx;
                        break;
                }
        }
        
        // Clean up any DONE contexts
        for (int i = 1; i < NUM_CTX; i++)
        {
                if (contexts[i].state == DONE && allocated_stacks[i] != NULL)
                {
                        free(allocated_stacks[i]);
                        allocated_stacks[i] = NULL;
                        memset(&contexts[i].context, 0, sizeof(ucontext_t));
                        contexts[i].state = INVALID;
                }
        }
        
        // If no other valid context found, only current context remains
        if (next == -1)
        {
                return 0;  // Only the caller remains
        }
        
        // Update the current context index
        current_context_idx = (uint8_t)next;
        
        // Swap contexts: save current, restore next
        swapcontext(&contexts[current].context, &contexts[next].context);
        
        // When we return here (after being swapped back), clean up DONE contexts
        for (int i = 1; i < NUM_CTX; i++)
        {
                if (contexts[i].state == DONE && allocated_stacks[i] != NULL)
                {
                        free(allocated_stacks[i]);
                        allocated_stacks[i] = NULL;
                        memset(&contexts[i].context, 0, sizeof(ucontext_t));
                        contexts[i].state = INVALID;
                }
        }
        
        // Count valid contexts
        int32_t count = 0;
        for (int i = 0; i < NUM_CTX; i++)
        {
                if (contexts[i].state == VALID && i != (int)current_context_idx)
                {
                        count++;
                }
        }
        
        return count;
}

void t_finish()
{
        // Get the current context index
        uint8_t current = current_context_idx;
        
        // Mark the context as DONE (don't free the stack - we're still on it!)
        contexts[current].state = DONE;
        
        // Find the next valid context to switch to
        int32_t next = -1;
        for (int i = 0; i < NUM_CTX; i++)
        {
                if (contexts[i].state == VALID)
                {
                        next = i;
                        break;
                }
        }
        
        // There should always be at least the main thread
        if (next != -1)
        {
                current_context_idx = (uint8_t)next;
                setcontext(&contexts[next].context);
        }
        
        // Should never reach here
        exit(1);
}

