.class public abstract Landroidx/viewpager2/adapter/FragmentStateAdapter;
.super Landroidx/recyclerview/widget/RecyclerView$Adapter;
.source "FragmentStateAdapter.java"

# interfaces
.implements Landroidx/viewpager2/adapter/StatefulAdapter;


# annotations
.annotation system Ldalvik/annotation/MemberClasses;
    value = {
        Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;,
        Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;,
        Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;,
        Landroidx/viewpager2/adapter/FragmentStateAdapter$ExperimentalFragmentStateAdapterApi;,
        Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;
    }
.end annotation

.annotation system Ldalvik/annotation/Signature;
    value = {
        "Landroidx/recyclerview/widget/RecyclerView$Adapter<",
        "Landroidx/viewpager2/adapter/FragmentViewHolder;",
        ">;",
        "Landroidx/viewpager2/adapter/StatefulAdapter;"
    }
.end annotation


# static fields
.field private static final GRACE_WINDOW_TIME_MS:J = 0x2710L

.field private static final KEY_PREFIX_FRAGMENT:Ljava/lang/String; = "f#"

.field private static final KEY_PREFIX_STATE:Ljava/lang/String; = "s#"


# instance fields
.field mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

.field final mFragmentManager:Landroidx/fragment/app/FragmentManager;

.field private mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

.field final mFragments:Landroidx/collection/LongSparseArray;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Landroidx/collection/LongSparseArray<",
            "Landroidx/fragment/app/Fragment;",
            ">;"
        }
    .end annotation
.end field

.field private mHasStaleFragments:Z

.field mIsInGracePeriod:Z

.field private final mItemIdToViewHolder:Landroidx/collection/LongSparseArray;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Landroidx/collection/LongSparseArray<",
            "Ljava/lang/Integer;",
            ">;"
        }
    .end annotation
.end field

.field final mLifecycle:Landroidx/lifecycle/Lifecycle;

.field private final mSavedStates:Landroidx/collection/LongSparseArray;
    .annotation system Ldalvik/annotation/Signature;
        value = {
            "Landroidx/collection/LongSparseArray<",
            "Landroidx/fragment/app/Fragment$SavedState;",
            ">;"
        }
    .end annotation
.end field


# direct methods
.method public constructor <init>(Landroidx/fragment/app/Fragment;)V
    .locals 2
    .param p1, "fragment"    # Landroidx/fragment/app/Fragment;

    .line 122
    invoke-virtual {p1}, Landroidx/fragment/app/Fragment;->getChildFragmentManager()Landroidx/fragment/app/FragmentManager;

    move-result-object v0

    invoke-virtual {p1}, Landroidx/fragment/app/Fragment;->getLifecycle()Landroidx/lifecycle/Lifecycle;

    move-result-object v1

    invoke-direct {p0, v0, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;-><init>(Landroidx/fragment/app/FragmentManager;Landroidx/lifecycle/Lifecycle;)V

    .line 123
    return-void
.end method

.method public constructor <init>(Landroidx/fragment/app/FragmentActivity;)V
    .locals 2
    .param p1, "fragmentActivity"    # Landroidx/fragment/app/FragmentActivity;

    .line 112
    invoke-virtual {p1}, Landroidx/fragment/app/FragmentActivity;->getSupportFragmentManager()Landroidx/fragment/app/FragmentManager;

    move-result-object v0

    invoke-virtual {p1}, Landroidx/fragment/app/FragmentActivity;->getLifecycle()Landroidx/lifecycle/Lifecycle;

    move-result-object v1

    invoke-direct {p0, v0, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;-><init>(Landroidx/fragment/app/FragmentManager;Landroidx/lifecycle/Lifecycle;)V

    .line 113
    return-void
.end method

.method public constructor <init>(Landroidx/fragment/app/FragmentManager;Landroidx/lifecycle/Lifecycle;)V
    .locals 1
    .param p1, "fragmentManager"    # Landroidx/fragment/app/FragmentManager;
    .param p2, "lifecycle"    # Landroidx/lifecycle/Lifecycle;

    .line 133
    invoke-direct {p0}, Landroidx/recyclerview/widget/RecyclerView$Adapter;-><init>()V

    .line 89
    new-instance v0, Landroidx/collection/LongSparseArray;

    invoke-direct {v0}, Landroidx/collection/LongSparseArray;-><init>()V

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    .line 91
    new-instance v0, Landroidx/collection/LongSparseArray;

    invoke-direct {v0}, Landroidx/collection/LongSparseArray;-><init>()V

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    .line 92
    new-instance v0, Landroidx/collection/LongSparseArray;

    invoke-direct {v0}, Landroidx/collection/LongSparseArray;-><init>()V

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    .line 96
    new-instance v0, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-direct {v0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;-><init>()V

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    .line 100
    const/4 v0, 0x0

    iput-boolean v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mIsInGracePeriod:Z

    .line 102
    iput-boolean v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mHasStaleFragments:Z

    .line 134
    iput-object p1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    .line 135
    iput-object p2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mLifecycle:Landroidx/lifecycle/Lifecycle;

    .line 136
    const/4 v0, 0x1

    invoke-super {p0, v0}, Landroidx/recyclerview/widget/RecyclerView$Adapter;->setHasStableIds(Z)V

    .line 137
    return-void
.end method

.method private static createKey(Ljava/lang/String;J)Ljava/lang/String;
    .locals 1
    .param p0, "prefix"    # Ljava/lang/String;
    .param p1, "id"    # J

    .line 617
    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V

    invoke-virtual {v0, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0, p1, p2}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object v0

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    return-object v0
.end method

.method private ensureFragment(I)V
    .locals 4
    .param p1, "position"    # I

    .line 263
    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->getItemId(I)J

    move-result-wide v0

    .line 264
    .local v0, "itemId":J
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v0, v1}, Landroidx/collection/LongSparseArray;->containsKey(J)Z

    move-result v2

    if-nez v2, :cond_0

    .line 266
    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->createFragment(I)Landroidx/fragment/app/Fragment;

    move-result-object v2

    .line 267
    .local v2, "newFragment":Landroidx/fragment/app/Fragment;
    iget-object v3, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v3, v0, v1}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v3

    check-cast v3, Landroidx/fragment/app/Fragment$SavedState;

    invoke-virtual {v2, v3}, Landroidx/fragment/app/Fragment;->setInitialSavedState(Landroidx/fragment/app/Fragment$SavedState;)V

    .line 268
    iget-object v3, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v3, v0, v1, v2}, Landroidx/collection/LongSparseArray;->put(JLjava/lang/Object;)V

    .line 270
    .end local v2    # "newFragment":Landroidx/fragment/app/Fragment;
    :cond_0
    return-void
.end method

.method private isFragmentViewBound(J)Z
    .locals 5
    .param p1, "itemId"    # J

    .line 231
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v0, p1, p2}, Landroidx/collection/LongSparseArray;->containsKey(J)Z

    move-result v0

    const/4 v1, 0x1

    if-eqz v0, :cond_0

    .line 232
    return v1

    .line 235
    :cond_0
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v0, p1, p2}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroidx/fragment/app/Fragment;

    .line 236
    .local v0, "fragment":Landroidx/fragment/app/Fragment;
    const/4 v2, 0x0

    if-nez v0, :cond_1

    .line 237
    return v2

    .line 240
    :cond_1
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->getView()Landroid/view/View;

    move-result-object v3

    .line 241
    .local v3, "view":Landroid/view/View;
    if-nez v3, :cond_2

    .line 242
    return v2

    .line 245
    :cond_2
    invoke-virtual {v3}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v4

    if-eqz v4, :cond_3

    goto :goto_0

    :cond_3
    move v1, v2

    :goto_0
    return v1
.end method

.method private static isValidKey(Ljava/lang/String;Ljava/lang/String;)Z
    .locals 2
    .param p0, "key"    # Ljava/lang/String;
    .param p1, "prefix"    # Ljava/lang/String;

    .line 622
    invoke-virtual {p0, p1}, Ljava/lang/String;->startsWith(Ljava/lang/String;)Z

    move-result v0

    if-eqz v0, :cond_0

    invoke-virtual {p0}, Ljava/lang/String;->length()I

    move-result v0

    invoke-virtual {p1}, Ljava/lang/String;->length()I

    move-result v1

    if-le v0, v1, :cond_0

    const/4 v0, 0x1

    goto :goto_0

    :cond_0
    const/4 v0, 0x0

    :goto_0
    return v0
.end method

.method private itemForViewHolder(I)Ljava/lang/Long;
    .locals 4
    .param p1, "viewHolderId"    # I

    .line 249
    const/4 v0, 0x0

    .line 250
    .local v0, "boundItemId":Ljava/lang/Long;
    const/4 v1, 0x0

    .local v1, "ix":I
    :goto_0
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    if-ge v1, v2, :cond_2

    .line 251
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->valueAt(I)Ljava/lang/Object;

    move-result-object v2

    check-cast v2, Ljava/lang/Integer;

    invoke-virtual {v2}, Ljava/lang/Integer;->intValue()I

    move-result v2

    if-ne v2, p1, :cond_1

    .line 252
    if-nez v0, :cond_0

    .line 256
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->keyAt(I)J

    move-result-wide v2

    invoke-static {v2, v3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v0

    goto :goto_1

    .line 253
    :cond_0
    new-instance v2, Ljava/lang/IllegalStateException;

    const-string v3, "Design assumption violated: a ViewHolder can only be bound to one item at a time."

    invoke-direct {v2, v3}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw v2

    .line 250
    :cond_1
    :goto_1
    add-int/lit8 v1, v1, 0x1

    goto :goto_0

    .line 259
    .end local v1    # "ix":I
    :cond_2
    return-object v0
.end method

.method private static parseIdFromKey(Ljava/lang/String;Ljava/lang/String;)J
    .locals 2
    .param p0, "key"    # Ljava/lang/String;
    .param p1, "prefix"    # Ljava/lang/String;

    .line 627
    invoke-virtual {p1}, Ljava/lang/String;->length()I

    move-result v0

    invoke-virtual {p0, v0}, Ljava/lang/String;->substring(I)Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Ljava/lang/Long;->parseLong(Ljava/lang/String;)J

    move-result-wide v0

    return-wide v0
.end method

.method private removeFragment(J)V
    .locals 4
    .param p1, "itemId"    # J

    .line 439
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v0, p1, p2}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroidx/fragment/app/Fragment;

    .line 441
    .local v0, "fragment":Landroidx/fragment/app/Fragment;
    if-nez v0, :cond_0

    .line 442
    return-void

    .line 445
    :cond_0
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->getView()Landroid/view/View;

    move-result-object v1

    if-eqz v1, :cond_1

    .line 446
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->getView()Landroid/view/View;

    move-result-object v1

    invoke-virtual {v1}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v1

    .line 447
    .local v1, "viewParent":Landroid/view/ViewParent;
    if-eqz v1, :cond_1

    .line 448
    move-object v2, v1

    check-cast v2, Landroid/widget/FrameLayout;

    invoke-virtual {v2}, Landroid/widget/FrameLayout;->removeAllViews()V

    .line 452
    .end local v1    # "viewParent":Landroid/view/ViewParent;
    :cond_1
    invoke-virtual {p0, p1, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->containsItem(J)Z

    move-result v1

    if-nez v1, :cond_2

    .line 453
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v1, p1, p2}, Landroidx/collection/LongSparseArray;->remove(J)V

    .line 456
    :cond_2
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v1

    if-nez v1, :cond_3

    .line 457
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v1, p1, p2}, Landroidx/collection/LongSparseArray;->remove(J)V

    .line 458
    return-void

    .line 461
    :cond_3
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->shouldDelayFragmentTransactions()Z

    move-result v1

    if-eqz v1, :cond_4

    .line 462
    const/4 v1, 0x1

    iput-boolean v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mHasStaleFragments:Z

    .line 463
    return-void

    .line 466
    :cond_4
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v1

    if-eqz v1, :cond_5

    invoke-virtual {p0, p1, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->containsItem(J)Z

    move-result v1

    if-eqz v1, :cond_5

    .line 467
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    .line 468
    invoke-virtual {v1, v0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPreSavedInstanceState(Landroidx/fragment/app/Fragment;)Ljava/util/List;

    move-result-object v1

    .line 469
    .local v1, "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v2, v0}, Landroidx/fragment/app/FragmentManager;->saveFragmentInstanceState(Landroidx/fragment/app/Fragment;)Landroidx/fragment/app/Fragment$SavedState;

    move-result-object v2

    .line 470
    .local v2, "savedState":Landroidx/fragment/app/Fragment$SavedState;
    iget-object v3, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v3, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPostEvents(Ljava/util/List;)V

    .line 472
    iget-object v3, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v3, p1, p2, v2}, Landroidx/collection/LongSparseArray;->put(JLjava/lang/Object;)V

    .line 474
    .end local v1    # "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    .end local v2    # "savedState":Landroidx/fragment/app/Fragment$SavedState;
    :cond_5
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    .line 475
    invoke-virtual {v1, v0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPreRemoved(Landroidx/fragment/app/Fragment;)Ljava/util/List;

    move-result-object v1

    .line 477
    .restart local v1    # "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    :try_start_0
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v2}, Landroidx/fragment/app/FragmentManager;->beginTransaction()Landroidx/fragment/app/FragmentTransaction;

    move-result-object v2

    invoke-virtual {v2, v0}, Landroidx/fragment/app/FragmentTransaction;->remove(Landroidx/fragment/app/Fragment;)Landroidx/fragment/app/FragmentTransaction;

    move-result-object v2

    invoke-virtual {v2}, Landroidx/fragment/app/FragmentTransaction;->commitNow()V

    .line 478
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, p1, p2}, Landroidx/collection/LongSparseArray;->remove(J)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    .line 480
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v2, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPostEvents(Ljava/util/List;)V

    .line 481
    nop

    .line 482
    return-void

    .line 480
    :catchall_0
    move-exception v2

    iget-object v3, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v3, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPostEvents(Ljava/util/List;)V

    .line 481
    throw v2
.end method

.method private scheduleGracePeriodEnd()V
    .locals 4

    .line 592
    new-instance v0, Landroid/os/Handler;

    invoke-static {}, Landroid/os/Looper;->getMainLooper()Landroid/os/Looper;

    move-result-object v1

    invoke-direct {v0, v1}, Landroid/os/Handler;-><init>(Landroid/os/Looper;)V

    .line 593
    .local v0, "handler":Landroid/os/Handler;
    new-instance v1, Landroidx/viewpager2/adapter/FragmentStateAdapter$3;

    invoke-direct {v1, p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$3;-><init>(Landroidx/viewpager2/adapter/FragmentStateAdapter;)V

    .line 601
    .local v1, "runnable":Ljava/lang/Runnable;
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mLifecycle:Landroidx/lifecycle/Lifecycle;

    new-instance v3, Landroidx/viewpager2/adapter/FragmentStateAdapter$4;

    invoke-direct {v3, p0, v0, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$4;-><init>(Landroidx/viewpager2/adapter/FragmentStateAdapter;Landroid/os/Handler;Ljava/lang/Runnable;)V

    invoke-virtual {v2, v3}, Landroidx/lifecycle/Lifecycle;->addObserver(Landroidx/lifecycle/LifecycleObserver;)V

    .line 612
    const-wide/16 v2, 0x2710

    invoke-virtual {v0, v1, v2, v3}, Landroid/os/Handler;->postDelayed(Ljava/lang/Runnable;J)Z

    .line 613
    return-void
.end method

.method private scheduleViewAttach(Landroidx/fragment/app/Fragment;Landroid/widget/FrameLayout;)V
    .locals 3
    .param p1, "fragment"    # Landroidx/fragment/app/Fragment;
    .param p2, "container"    # Landroid/widget/FrameLayout;

    .line 373
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    new-instance v1, Landroidx/viewpager2/adapter/FragmentStateAdapter$2;

    invoke-direct {v1, p0, p1, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter$2;-><init>(Landroidx/viewpager2/adapter/FragmentStateAdapter;Landroidx/fragment/app/Fragment;Landroid/widget/FrameLayout;)V

    const/4 v2, 0x0

    invoke-virtual {v0, v1, v2}, Landroidx/fragment/app/FragmentManager;->registerFragmentLifecycleCallbacks(Landroidx/fragment/app/FragmentManager$FragmentLifecycleCallbacks;Z)V

    .line 387
    return-void
.end method


# virtual methods
.method addViewToContainer(Landroid/view/View;Landroid/widget/FrameLayout;)V
    .locals 2
    .param p1, "v"    # Landroid/view/View;
    .param p2, "container"    # Landroid/widget/FrameLayout;

    .line 391
    invoke-virtual {p2}, Landroid/widget/FrameLayout;->getChildCount()I

    move-result v0

    const/4 v1, 0x1

    if-gt v0, v1, :cond_3

    .line 395
    invoke-virtual {p1}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v0

    if-ne v0, p2, :cond_0

    .line 396
    return-void

    .line 399
    :cond_0
    invoke-virtual {p2}, Landroid/widget/FrameLayout;->getChildCount()I

    move-result v0

    if-lez v0, :cond_1

    .line 400
    invoke-virtual {p2}, Landroid/widget/FrameLayout;->removeAllViews()V

    .line 403
    :cond_1
    invoke-virtual {p1}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v0

    if-eqz v0, :cond_2

    .line 404
    invoke-virtual {p1}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v0

    check-cast v0, Landroid/view/ViewGroup;

    invoke-virtual {v0, p1}, Landroid/view/ViewGroup;->removeView(Landroid/view/View;)V

    .line 407
    :cond_2
    invoke-virtual {p2, p1}, Landroid/widget/FrameLayout;->addView(Landroid/view/View;)V

    .line 408
    return-void

    .line 392
    :cond_3
    new-instance v0, Ljava/lang/IllegalStateException;

    const-string v1, "Design assumption violated."

    invoke-direct {v0, v1}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw v0
.end method

.method public containsItem(J)Z
    .locals 2
    .param p1, "itemId"    # J

    .line 512
    const-wide/16 v0, 0x0

    cmp-long v0, p1, v0

    if-ltz v0, :cond_0

    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->getItemCount()I

    move-result v0

    int-to-long v0, v0

    cmp-long v0, p1, v0

    if-gez v0, :cond_0

    const/4 v0, 0x1

    goto :goto_0

    :cond_0
    const/4 v0, 0x0

    :goto_0
    return v0
.end method

.method public abstract createFragment(I)Landroidx/fragment/app/Fragment;
.end method

.method gcFragments()V
    .locals 5

    .line 199
    iget-boolean v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mHasStaleFragments:Z

    if-eqz v0, :cond_6

    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->shouldDelayFragmentTransactions()Z

    move-result v0

    if-eqz v0, :cond_0

    goto :goto_3

    .line 204
    :cond_0
    new-instance v0, Landroidx/collection/ArraySet;

    invoke-direct {v0}, Landroidx/collection/ArraySet;-><init>()V

    .line 205
    .local v0, "toRemove":Ljava/util/Set;, "Ljava/util/Set<Ljava/lang/Long;>;"
    const/4 v1, 0x0

    .local v1, "ix":I
    :goto_0
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    if-ge v1, v2, :cond_2

    .line 206
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->keyAt(I)J

    move-result-wide v2

    .line 207
    .local v2, "itemId":J
    invoke-virtual {p0, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->containsItem(J)Z

    move-result v4

    if-nez v4, :cond_1

    .line 208
    invoke-static {v2, v3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v4

    invoke-interface {v0, v4}, Ljava/util/Set;->add(Ljava/lang/Object;)Z

    .line 209
    iget-object v4, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v4, v2, v3}, Landroidx/collection/LongSparseArray;->remove(J)V

    .line 205
    .end local v2    # "itemId":J
    :cond_1
    add-int/lit8 v1, v1, 0x1

    goto :goto_0

    .line 214
    .end local v1    # "ix":I
    :cond_2
    iget-boolean v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mIsInGracePeriod:Z

    if-nez v1, :cond_4

    .line 215
    const/4 v1, 0x0

    iput-boolean v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mHasStaleFragments:Z

    .line 217
    const/4 v1, 0x0

    .restart local v1    # "ix":I
    :goto_1
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    if-ge v1, v2, :cond_4

    .line 218
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->keyAt(I)J

    move-result-wide v2

    .line 219
    .restart local v2    # "itemId":J
    invoke-direct {p0, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->isFragmentViewBound(J)Z

    move-result v4

    if-nez v4, :cond_3

    .line 220
    invoke-static {v2, v3}, Ljava/lang/Long;->valueOf(J)Ljava/lang/Long;

    move-result-object v4

    invoke-interface {v0, v4}, Ljava/util/Set;->add(Ljava/lang/Object;)Z

    .line 217
    .end local v2    # "itemId":J
    :cond_3
    add-int/lit8 v1, v1, 0x1

    goto :goto_1

    .line 225
    .end local v1    # "ix":I
    :cond_4
    invoke-interface {v0}, Ljava/util/Set;->iterator()Ljava/util/Iterator;

    move-result-object v1

    :goto_2
    invoke-interface {v1}, Ljava/util/Iterator;->hasNext()Z

    move-result v2

    if-eqz v2, :cond_5

    invoke-interface {v1}, Ljava/util/Iterator;->next()Ljava/lang/Object;

    move-result-object v2

    check-cast v2, Ljava/lang/Long;

    .line 226
    .local v2, "itemId":Ljava/lang/Long;
    invoke-virtual {v2}, Ljava/lang/Long;->longValue()J

    move-result-wide v3

    invoke-direct {p0, v3, v4}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->removeFragment(J)V

    .line 227
    .end local v2    # "itemId":Ljava/lang/Long;
    goto :goto_2

    .line 228
    :cond_5
    return-void

    .line 200
    .end local v0    # "toRemove":Ljava/util/Set;, "Ljava/util/Set<Ljava/lang/Long;>;"
    :cond_6
    :goto_3
    return-void
.end method

.method public getItemId(I)J
    .locals 2
    .param p1, "position"    # I

    .line 502
    int-to-long v0, p1

    return-wide v0
.end method

.method public onAttachedToRecyclerView(Landroidx/recyclerview/widget/RecyclerView;)V
    .locals 1
    .param p1, "recyclerView"    # Landroidx/recyclerview/widget/RecyclerView;

    .line 142
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    if-nez v0, :cond_0

    const/4 v0, 0x1

    goto :goto_0

    :cond_0
    const/4 v0, 0x0

    :goto_0
    invoke-static {v0}, Landroidx/core/util/Preconditions;->checkArgument(Z)V

    .line 143
    new-instance v0, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    invoke-direct {v0, p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;-><init>(Landroidx/viewpager2/adapter/FragmentStateAdapter;)V

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    .line 144
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    invoke-virtual {v0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;->register(Landroidx/recyclerview/widget/RecyclerView;)V

    .line 145
    return-void
.end method

.method public bridge synthetic onBindViewHolder(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;I)V
    .locals 0

    .line 74
    check-cast p1, Landroidx/viewpager2/adapter/FragmentViewHolder;

    invoke-virtual {p0, p1, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->onBindViewHolder(Landroidx/viewpager2/adapter/FragmentViewHolder;I)V

    return-void
.end method

.method public final onBindViewHolder(Landroidx/viewpager2/adapter/FragmentViewHolder;I)V
    .locals 7
    .param p1, "holder"    # Landroidx/viewpager2/adapter/FragmentViewHolder;
    .param p2, "position"    # I

    .line 176
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getItemId()J

    move-result-wide v0

    .line 177
    .local v0, "itemId":J
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getContainer()Landroid/widget/FrameLayout;

    move-result-object v2

    invoke-virtual {v2}, Landroid/widget/FrameLayout;->getId()I

    move-result v2

    .line 178
    .local v2, "viewHolderId":I
    invoke-direct {p0, v2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->itemForViewHolder(I)Ljava/lang/Long;

    move-result-object v3

    .line 179
    .local v3, "boundItemId":Ljava/lang/Long;
    if-eqz v3, :cond_0

    invoke-virtual {v3}, Ljava/lang/Long;->longValue()J

    move-result-wide v4

    cmp-long v4, v4, v0

    if-eqz v4, :cond_0

    .line 180
    invoke-virtual {v3}, Ljava/lang/Long;->longValue()J

    move-result-wide v4

    invoke-direct {p0, v4, v5}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->removeFragment(J)V

    .line 181
    iget-object v4, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v3}, Ljava/lang/Long;->longValue()J

    move-result-wide v5

    invoke-virtual {v4, v5, v6}, Landroidx/collection/LongSparseArray;->remove(J)V

    .line 184
    :cond_0
    iget-object v4, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-static {v2}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object v5

    invoke-virtual {v4, v0, v1, v5}, Landroidx/collection/LongSparseArray;->put(JLjava/lang/Object;)V

    .line 185
    invoke-direct {p0, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->ensureFragment(I)V

    .line 189
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getContainer()Landroid/widget/FrameLayout;

    move-result-object v4

    .line 190
    .local v4, "container":Landroid/widget/FrameLayout;
    invoke-static {v4}, Landroidx/core/view/ViewCompat;->isAttachedToWindow(Landroid/view/View;)Z

    move-result v5

    if-eqz v5, :cond_1

    .line 191
    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->placeFragmentInViewHolder(Landroidx/viewpager2/adapter/FragmentViewHolder;)V

    .line 194
    :cond_1
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->gcFragments()V

    .line 195
    return-void
.end method

.method public bridge synthetic onCreateViewHolder(Landroid/view/ViewGroup;I)Landroidx/recyclerview/widget/RecyclerView$ViewHolder;
    .locals 0

    .line 74
    invoke-virtual {p0, p1, p2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->onCreateViewHolder(Landroid/view/ViewGroup;I)Landroidx/viewpager2/adapter/FragmentViewHolder;

    move-result-object p1

    return-object p1
.end method

.method public final onCreateViewHolder(Landroid/view/ViewGroup;I)Landroidx/viewpager2/adapter/FragmentViewHolder;
    .locals 1
    .param p1, "parent"    # Landroid/view/ViewGroup;
    .param p2, "viewType"    # I

    .line 171
    invoke-static {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->create(Landroid/view/ViewGroup;)Landroidx/viewpager2/adapter/FragmentViewHolder;

    move-result-object v0

    return-object v0
.end method

.method public onDetachedFromRecyclerView(Landroidx/recyclerview/widget/RecyclerView;)V
    .locals 1
    .param p1, "recyclerView"    # Landroidx/recyclerview/widget/RecyclerView;

    .line 150
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    invoke-virtual {v0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;->unregister(Landroidx/recyclerview/widget/RecyclerView;)V

    .line 151
    const/4 v0, 0x0

    iput-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    .line 152
    return-void
.end method

.method public bridge synthetic onFailedToRecycleView(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;)Z
    .locals 0

    .line 74
    check-cast p1, Landroidx/viewpager2/adapter/FragmentViewHolder;

    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->onFailedToRecycleView(Landroidx/viewpager2/adapter/FragmentViewHolder;)Z

    move-result p1

    return p1
.end method

.method public final onFailedToRecycleView(Landroidx/viewpager2/adapter/FragmentViewHolder;)Z
    .locals 1
    .param p1, "holder"    # Landroidx/viewpager2/adapter/FragmentViewHolder;

    .line 435
    const/4 v0, 0x1

    return v0
.end method

.method public bridge synthetic onViewAttachedToWindow(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;)V
    .locals 0

    .line 74
    check-cast p1, Landroidx/viewpager2/adapter/FragmentViewHolder;

    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->onViewAttachedToWindow(Landroidx/viewpager2/adapter/FragmentViewHolder;)V

    return-void
.end method

.method public final onViewAttachedToWindow(Landroidx/viewpager2/adapter/FragmentViewHolder;)V
    .locals 0
    .param p1, "holder"    # Landroidx/viewpager2/adapter/FragmentViewHolder;

    .line 274
    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->placeFragmentInViewHolder(Landroidx/viewpager2/adapter/FragmentViewHolder;)V

    .line 275
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->gcFragments()V

    .line 276
    return-void
.end method

.method public bridge synthetic onViewRecycled(Landroidx/recyclerview/widget/RecyclerView$ViewHolder;)V
    .locals 0

    .line 74
    check-cast p1, Landroidx/viewpager2/adapter/FragmentViewHolder;

    invoke-virtual {p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->onViewRecycled(Landroidx/viewpager2/adapter/FragmentViewHolder;)V

    return-void
.end method

.method public final onViewRecycled(Landroidx/viewpager2/adapter/FragmentViewHolder;)V
    .locals 5
    .param p1, "holder"    # Landroidx/viewpager2/adapter/FragmentViewHolder;

    .line 412
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getContainer()Landroid/widget/FrameLayout;

    move-result-object v0

    invoke-virtual {v0}, Landroid/widget/FrameLayout;->getId()I

    move-result v0

    .line 413
    .local v0, "viewHolderId":I
    invoke-direct {p0, v0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->itemForViewHolder(I)Ljava/lang/Long;

    move-result-object v1

    .line 414
    .local v1, "boundItemId":Ljava/lang/Long;
    if-eqz v1, :cond_0

    .line 415
    invoke-virtual {v1}, Ljava/lang/Long;->longValue()J

    move-result-wide v2

    invoke-direct {p0, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->removeFragment(J)V

    .line 416
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mItemIdToViewHolder:Landroidx/collection/LongSparseArray;

    invoke-virtual {v1}, Ljava/lang/Long;->longValue()J

    move-result-wide v3

    invoke-virtual {v2, v3, v4}, Landroidx/collection/LongSparseArray;->remove(J)V

    .line 418
    :cond_0
    return-void
.end method

.method placeFragmentInViewHolder(Landroidx/viewpager2/adapter/FragmentViewHolder;)V
    .locals 9
    .param p1, "holder"    # Landroidx/viewpager2/adapter/FragmentViewHolder;

    .line 283
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getItemId()J

    move-result-wide v1

    invoke-virtual {v0, v1, v2}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Landroidx/fragment/app/Fragment;

    .line 284
    .local v0, "fragment":Landroidx/fragment/app/Fragment;
    const-string v1, "Design assumption violated."

    if-eqz v0, :cond_8

    .line 287
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getContainer()Landroid/widget/FrameLayout;

    move-result-object v2

    .line 288
    .local v2, "container":Landroid/widget/FrameLayout;
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->getView()Landroid/view/View;

    move-result-object v3

    .line 309
    .local v3, "view":Landroid/view/View;
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v4

    if-nez v4, :cond_1

    if-nez v3, :cond_0

    goto :goto_0

    .line 310
    :cond_0
    new-instance v4, Ljava/lang/IllegalStateException;

    invoke-direct {v4, v1}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw v4

    .line 314
    :cond_1
    :goto_0
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v1

    if-eqz v1, :cond_2

    if-nez v3, :cond_2

    .line 315
    invoke-direct {p0, v0, v2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->scheduleViewAttach(Landroidx/fragment/app/Fragment;Landroid/widget/FrameLayout;)V

    .line 316
    return-void

    .line 320
    :cond_2
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v1

    if-eqz v1, :cond_4

    invoke-virtual {v3}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v1

    if-eqz v1, :cond_4

    .line 321
    invoke-virtual {v3}, Landroid/view/View;->getParent()Landroid/view/ViewParent;

    move-result-object v1

    if-eq v1, v2, :cond_3

    .line 322
    invoke-virtual {p0, v3, v2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->addViewToContainer(Landroid/view/View;Landroid/widget/FrameLayout;)V

    .line 324
    :cond_3
    return-void

    .line 328
    :cond_4
    invoke-virtual {v0}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v1

    if-eqz v1, :cond_5

    .line 329
    invoke-virtual {p0, v3, v2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->addViewToContainer(Landroid/view/View;Landroid/widget/FrameLayout;)V

    .line 330
    return-void

    .line 334
    :cond_5
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->shouldDelayFragmentTransactions()Z

    move-result v1

    if-nez v1, :cond_6

    .line 335
    invoke-direct {p0, v0, v2}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->scheduleViewAttach(Landroidx/fragment/app/Fragment;Landroid/widget/FrameLayout;)V

    .line 336
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    .line 337
    invoke-virtual {v1, v0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPreAdded(Landroidx/fragment/app/Fragment;)Ljava/util/List;

    move-result-object v1

    .line 339
    .local v1, "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    const/4 v4, 0x0

    :try_start_0
    invoke-virtual {v0, v4}, Landroidx/fragment/app/Fragment;->setMenuVisibility(Z)V

    .line 340
    iget-object v5, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v5}, Landroidx/fragment/app/FragmentManager;->beginTransaction()Landroidx/fragment/app/FragmentTransaction;

    move-result-object v5

    new-instance v6, Ljava/lang/StringBuilder;

    invoke-direct {v6}, Ljava/lang/StringBuilder;-><init>()V

    const-string v7, "f"

    invoke-virtual {v6, v7}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v6

    .line 341
    invoke-virtual {p1}, Landroidx/viewpager2/adapter/FragmentViewHolder;->getItemId()J

    move-result-wide v7

    invoke-virtual {v6, v7, v8}, Ljava/lang/StringBuilder;->append(J)Ljava/lang/StringBuilder;

    move-result-object v6

    invoke-virtual {v6}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v6

    invoke-virtual {v5, v0, v6}, Landroidx/fragment/app/FragmentTransaction;->add(Landroidx/fragment/app/Fragment;Ljava/lang/String;)Landroidx/fragment/app/FragmentTransaction;

    move-result-object v5

    sget-object v6, Landroidx/lifecycle/Lifecycle$State;->STARTED:Landroidx/lifecycle/Lifecycle$State;

    .line 342
    invoke-virtual {v5, v0, v6}, Landroidx/fragment/app/FragmentTransaction;->setMaxLifecycle(Landroidx/fragment/app/Fragment;Landroidx/lifecycle/Lifecycle$State;)Landroidx/fragment/app/FragmentTransaction;

    move-result-object v5

    .line 343
    invoke-virtual {v5}, Landroidx/fragment/app/FragmentTransaction;->commitNow()V

    .line 344
    iget-object v5, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentMaxLifecycleEnforcer:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;

    invoke-virtual {v5, v4}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentMaxLifecycleEnforcer;->updateFragmentMaxLifecycle(Z)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catchall_0

    .line 346
    iget-object v4, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v4, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPostEvents(Ljava/util/List;)V

    .line 347
    nop

    .line 348
    .end local v1    # "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    goto :goto_1

    .line 346
    .restart local v1    # "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    :catchall_0
    move-exception v4

    iget-object v5, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v5, v1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->dispatchPostEvents(Ljava/util/List;)V

    .line 347
    throw v4

    .line 349
    .end local v1    # "onPost":Ljava/util/List;, "Ljava/util/List<Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback$OnPostEventListener;>;"
    :cond_6
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v1}, Landroidx/fragment/app/FragmentManager;->isDestroyed()Z

    move-result v1

    if-eqz v1, :cond_7

    .line 350
    return-void

    .line 352
    :cond_7
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mLifecycle:Landroidx/lifecycle/Lifecycle;

    new-instance v4, Landroidx/viewpager2/adapter/FragmentStateAdapter$1;

    invoke-direct {v4, p0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$1;-><init>(Landroidx/viewpager2/adapter/FragmentStateAdapter;Landroidx/viewpager2/adapter/FragmentViewHolder;)V

    invoke-virtual {v1, v4}, Landroidx/lifecycle/Lifecycle;->addObserver(Landroidx/lifecycle/LifecycleObserver;)V

    .line 366
    :goto_1
    return-void

    .line 285
    .end local v2    # "container":Landroid/widget/FrameLayout;
    .end local v3    # "view":Landroid/view/View;
    :cond_8
    new-instance v2, Ljava/lang/IllegalStateException;

    invoke-direct {v2, v1}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw v2
.end method

.method public registerFragmentTransactionCallback(Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;)V
    .locals 1
    .param p1, "callback"    # Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;

    .line 931
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->registerCallback(Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;)V

    .line 932
    return-void
.end method

.method public final restoreState(Landroid/os/Parcelable;)V
    .locals 7
    .param p1, "savedState"    # Landroid/os/Parcelable;

    .line 552
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v0}, Landroidx/collection/LongSparseArray;->isEmpty()Z

    move-result v0

    if-eqz v0, :cond_6

    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v0}, Landroidx/collection/LongSparseArray;->isEmpty()Z

    move-result v0

    if-eqz v0, :cond_6

    .line 557
    move-object v0, p1

    check-cast v0, Landroid/os/Bundle;

    .line 558
    .local v0, "bundle":Landroid/os/Bundle;
    invoke-virtual {v0}, Landroid/os/Bundle;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v1

    if-nez v1, :cond_0

    .line 560
    invoke-virtual {p0}, Ljava/lang/Object;->getClass()Ljava/lang/Class;

    move-result-object v1

    invoke-virtual {v1}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v1

    invoke-virtual {v0, v1}, Landroid/os/Bundle;->setClassLoader(Ljava/lang/ClassLoader;)V

    .line 563
    :cond_0
    invoke-virtual {v0}, Landroid/os/Bundle;->keySet()Ljava/util/Set;

    move-result-object v1

    invoke-interface {v1}, Ljava/util/Set;->iterator()Ljava/util/Iterator;

    move-result-object v1

    :cond_1
    :goto_0
    invoke-interface {v1}, Ljava/util/Iterator;->hasNext()Z

    move-result v2

    if-eqz v2, :cond_4

    invoke-interface {v1}, Ljava/util/Iterator;->next()Ljava/lang/Object;

    move-result-object v2

    check-cast v2, Ljava/lang/String;

    .line 564
    .local v2, "key":Ljava/lang/String;
    const-string v3, "f#"

    invoke-static {v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->isValidKey(Ljava/lang/String;Ljava/lang/String;)Z

    move-result v4

    if-eqz v4, :cond_2

    .line 565
    invoke-static {v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->parseIdFromKey(Ljava/lang/String;Ljava/lang/String;)J

    move-result-wide v3

    .line 566
    .local v3, "itemId":J
    iget-object v5, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v5, v0, v2}, Landroidx/fragment/app/FragmentManager;->getFragment(Landroid/os/Bundle;Ljava/lang/String;)Landroidx/fragment/app/Fragment;

    move-result-object v5

    .line 567
    .local v5, "fragment":Landroidx/fragment/app/Fragment;
    iget-object v6, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v6, v3, v4, v5}, Landroidx/collection/LongSparseArray;->put(JLjava/lang/Object;)V

    .line 568
    goto :goto_0

    .line 571
    .end local v3    # "itemId":J
    .end local v5    # "fragment":Landroidx/fragment/app/Fragment;
    :cond_2
    const-string v3, "s#"

    invoke-static {v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->isValidKey(Ljava/lang/String;Ljava/lang/String;)Z

    move-result v4

    if-eqz v4, :cond_3

    .line 572
    invoke-static {v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->parseIdFromKey(Ljava/lang/String;Ljava/lang/String;)J

    move-result-wide v3

    .line 573
    .restart local v3    # "itemId":J
    invoke-virtual {v0, v2}, Landroid/os/Bundle;->getParcelable(Ljava/lang/String;)Landroid/os/Parcelable;

    move-result-object v5

    check-cast v5, Landroidx/fragment/app/Fragment$SavedState;

    .line 574
    .local v5, "state":Landroidx/fragment/app/Fragment$SavedState;
    invoke-virtual {p0, v3, v4}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->containsItem(J)Z

    move-result v6

    if-eqz v6, :cond_1

    .line 575
    iget-object v6, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v6, v3, v4, v5}, Landroidx/collection/LongSparseArray;->put(JLjava/lang/Object;)V

    goto :goto_0

    .line 580
    .end local v3    # "itemId":J
    .end local v5    # "state":Landroidx/fragment/app/Fragment$SavedState;
    :cond_3
    new-instance v1, Ljava/lang/IllegalArgumentException;

    new-instance v3, Ljava/lang/StringBuilder;

    invoke-direct {v3}, Ljava/lang/StringBuilder;-><init>()V

    const-string v4, "Unexpected key in savedState: "

    invoke-virtual {v3, v4}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    move-result-object v3

    invoke-virtual {v3}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v3

    invoke-direct {v1, v3}, Ljava/lang/IllegalArgumentException;-><init>(Ljava/lang/String;)V

    throw v1

    .line 583
    .end local v2    # "key":Ljava/lang/String;
    :cond_4
    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v1}, Landroidx/collection/LongSparseArray;->isEmpty()Z

    move-result v1

    if-nez v1, :cond_5

    .line 584
    const/4 v1, 0x1

    iput-boolean v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mHasStaleFragments:Z

    .line 585
    iput-boolean v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mIsInGracePeriod:Z

    .line 586
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->gcFragments()V

    .line 587
    invoke-direct {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->scheduleGracePeriodEnd()V

    .line 589
    :cond_5
    return-void

    .line 553
    .end local v0    # "bundle":Landroid/os/Bundle;
    :cond_6
    new-instance v0, Ljava/lang/IllegalStateException;

    const-string v1, "Expected the adapter to be \'fresh\' while restoring state."

    invoke-direct {v0, v1}, Ljava/lang/IllegalStateException;-><init>(Ljava/lang/String;)V

    throw v0
.end method

.method public final saveState()Landroid/os/Parcelable;
    .locals 7

    .line 525
    new-instance v0, Landroid/os/Bundle;

    iget-object v1, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v1}, Landroidx/collection/LongSparseArray;->size()I

    move-result v1

    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    add-int/2addr v1, v2

    invoke-direct {v0, v1}, Landroid/os/Bundle;-><init>(I)V

    .line 528
    .local v0, "savedState":Landroid/os/Bundle;
    const/4 v1, 0x0

    .local v1, "ix":I
    :goto_0
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    if-ge v1, v2, :cond_1

    .line 529
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->keyAt(I)J

    move-result-wide v2

    .line 530
    .local v2, "itemId":J
    iget-object v4, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragments:Landroidx/collection/LongSparseArray;

    invoke-virtual {v4, v2, v3}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v4

    check-cast v4, Landroidx/fragment/app/Fragment;

    .line 531
    .local v4, "fragment":Landroidx/fragment/app/Fragment;
    if-eqz v4, :cond_0

    invoke-virtual {v4}, Landroidx/fragment/app/Fragment;->isAdded()Z

    move-result v5

    if-eqz v5, :cond_0

    .line 532
    const-string v5, "f#"

    invoke-static {v5, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->createKey(Ljava/lang/String;J)Ljava/lang/String;

    move-result-object v5

    .line 533
    .local v5, "key":Ljava/lang/String;
    iget-object v6, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v6, v0, v5, v4}, Landroidx/fragment/app/FragmentManager;->putFragment(Landroid/os/Bundle;Ljava/lang/String;Landroidx/fragment/app/Fragment;)V

    .line 528
    .end local v2    # "itemId":J
    .end local v4    # "fragment":Landroidx/fragment/app/Fragment;
    .end local v5    # "key":Ljava/lang/String;
    :cond_0
    add-int/lit8 v1, v1, 0x1

    goto :goto_0

    .line 538
    .end local v1    # "ix":I
    :cond_1
    const/4 v1, 0x0

    .restart local v1    # "ix":I
    :goto_1
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2}, Landroidx/collection/LongSparseArray;->size()I

    move-result v2

    if-ge v1, v2, :cond_3

    .line 539
    iget-object v2, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v2, v1}, Landroidx/collection/LongSparseArray;->keyAt(I)J

    move-result-wide v2

    .line 540
    .restart local v2    # "itemId":J
    invoke-virtual {p0, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->containsItem(J)Z

    move-result v4

    if-eqz v4, :cond_2

    .line 541
    const-string v4, "s#"

    invoke-static {v4, v2, v3}, Landroidx/viewpager2/adapter/FragmentStateAdapter;->createKey(Ljava/lang/String;J)Ljava/lang/String;

    move-result-object v4

    .line 542
    .local v4, "key":Ljava/lang/String;
    iget-object v5, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mSavedStates:Landroidx/collection/LongSparseArray;

    invoke-virtual {v5, v2, v3}, Landroidx/collection/LongSparseArray;->get(J)Ljava/lang/Object;

    move-result-object v5

    check-cast v5, Landroid/os/Parcelable;

    invoke-virtual {v0, v4, v5}, Landroid/os/Bundle;->putParcelable(Ljava/lang/String;Landroid/os/Parcelable;)V

    .line 538
    .end local v2    # "itemId":J
    .end local v4    # "key":Ljava/lang/String;
    :cond_2
    add-int/lit8 v1, v1, 0x1

    goto :goto_1

    .line 546
    .end local v1    # "ix":I
    :cond_3
    return-object v0
.end method

.method public final setHasStableIds(Z)V
    .locals 2
    .param p1, "hasStableIds"    # Z

    .line 517
    new-instance v0, Ljava/lang/UnsupportedOperationException;

    const-string v1, "Stable Ids are required for the adapter to function properly, and the adapter takes care of setting the flag."

    invoke-direct {v0, v1}, Ljava/lang/UnsupportedOperationException;-><init>(Ljava/lang/String;)V

    throw v0
.end method

.method shouldDelayFragmentTransactions()Z
    .locals 1

    .line 486
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentManager:Landroidx/fragment/app/FragmentManager;

    invoke-virtual {v0}, Landroidx/fragment/app/FragmentManager;->isStateSaved()Z

    move-result v0

    return v0
.end method

.method public unregisterFragmentTransactionCallback(Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;)V
    .locals 1
    .param p1, "callback"    # Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;

    .line 942
    iget-object v0, p0, Landroidx/viewpager2/adapter/FragmentStateAdapter;->mFragmentEventDispatcher:Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;

    invoke-virtual {v0, p1}, Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentEventDispatcher;->unregisterCallback(Landroidx/viewpager2/adapter/FragmentStateAdapter$FragmentTransactionCallback;)V

    .line 943
    return-void
.end method
