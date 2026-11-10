.class abstract Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;
.super Landroidx/recyclerview/widget/RecyclerView$AdapterDataObserver;
.source "FragmentStateAdapter.java"


# annotations
.annotation system Ldalvik/annotation/EnclosingClass;
    value = Landroidx/viewpager2/adapter/FragmentStateAdapter;
.end annotation

.annotation system Ldalvik/annotation/InnerClass;
    accessFlags = 0x40a
    name = "DataSetChangeObserver"
.end annotation


# direct methods
.method private constructor <init>()V
    .locals 0

    .line 769
    invoke-direct {p0}, Landroidx/recyclerview/widget/RecyclerView$AdapterDataObserver;-><init>()V

    return-void
.end method

.method synthetic constructor <init>(Landroidx/viewpager2/adapter/FragmentStateAdapter$1;)V
    .locals 0
    .param p1, "x0"    # Landroidx/viewpager2/adapter/FragmentStateAdapter$1;

    .line 769
    invoke-direct {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;-><init>()V

    return-void
.end method


# virtual methods
.method public abstract onChanged()V
.end method

.method public final onItemRangeChanged(II)V
    .locals 0
    .param p1, "positionStart"    # I
    .param p2, "itemCount"    # I

    .line 775
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;->onChanged()V

    .line 776
    return-void
.end method

.method public final onItemRangeChanged(IILjava/lang/Object;)V
    .locals 0
    .param p1, "positionStart"    # I
    .param p2, "itemCount"    # I
    .param p3, "payload"    # Ljava/lang/Object;

    .line 781
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;->onChanged()V

    .line 782
    return-void
.end method

.method public final onItemRangeInserted(II)V
    .locals 0
    .param p1, "positionStart"    # I
    .param p2, "itemCount"    # I

    .line 786
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;->onChanged()V

    .line 787
    return-void
.end method

.method public final onItemRangeMoved(III)V
    .locals 0
    .param p1, "fromPosition"    # I
    .param p2, "toPosition"    # I
    .param p3, "itemCount"    # I

    .line 796
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;->onChanged()V

    .line 797
    return-void
.end method

.method public final onItemRangeRemoved(II)V
    .locals 0
    .param p1, "positionStart"    # I
    .param p2, "itemCount"    # I

    .line 791
    invoke-virtual {p0}, Landroidx/viewpager2/adapter/FragmentStateAdapter$DataSetChangeObserver;->onChanged()V

    .line 792
    return-void
.end method
